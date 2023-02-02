/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
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

#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/irq.h"
#include "hw/isa/apm.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/piix4.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/xen.h"
#include "qapi/error.h"
#include "qemu/range.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/acpi/cpu.h"
#include "hw/hotplug.h"
#include "hw/mem/pc-dimm.h"
#include "hw/mem/nvdimm.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "migration/vmstate.h"
#include "hw/core/cpu.h"
#include "trace.h"
#include "qom/object.h"

#define GPE_BASE 0xafe0
#define GPE_LEN 4

#define ACPI_PCIHP_ADDR_PIIX4 0xae00

struct pci_status {
    uint32_t up; /* deprecated, maintained for migration compatibility */
    uint32_t down;
};

static void piix4_acpi_system_hot_add_init(MemoryRegion *parent,
                                           PCIBus *bus, PIIX4PMState *s);

#define ACPI_ENABLE 0xf1
#define ACPI_DISABLE 0xf0

static void pm_tmr_timer(ACPIREGS *ar)
{
    PIIX4PMState *s = container_of(ar, PIIX4PMState, ar);
    acpi_update_sci(&s->ar, s->irq);
}

static void apm_ctrl_changed(uint32_t val, void *arg)
{
    PIIX4PMState *s = arg;
    PCIDevice *d = PCI_DEVICE(s);

    /* ACPI specs 3.0, 4.7.2.5 */
    acpi_pm1_cnt_update(&s->ar, val == ACPI_ENABLE, val == ACPI_DISABLE);
    if (val == ACPI_ENABLE || val == ACPI_DISABLE) {
        return;
    }

    if (d->config[0x5b] & (1 << 1)) {
        if (s->smi_irq) {
            qemu_irq_raise(s->smi_irq);
        }
    }
}

static void pm_io_space_update(PIIX4PMState *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    s->io_base = le32_to_cpu(*(uint32_t *)(d->config + 0x40));
    s->io_base &= 0xffc0;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->io, d->config[0x80] & 1);
    memory_region_set_address(&s->io, s->io_base);
    memory_region_transaction_commit();
}

static void smbus_io_space_update(PIIX4PMState *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    s->smb_io_base = le32_to_cpu(*(uint32_t *)(d->config + 0x90));
    s->smb_io_base &= 0xffc0;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->smb.io, d->config[0xd2] & 1);
    memory_region_set_address(&s->smb.io, s->smb_io_base);
    memory_region_transaction_commit();
}

static void pm_write_config(PCIDevice *d,
                            uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(d, address, val, len);
    if (range_covers_byte(address, len, 0x80) ||
        ranges_overlap(address, len, 0x40, 4)) {
        pm_io_space_update((PIIX4PMState *)d);
    }
    if (range_covers_byte(address, len, 0xd2) ||
        ranges_overlap(address, len, 0x90, 4)) {
        smbus_io_space_update((PIIX4PMState *)d);
    }
}

static int vmstate_acpi_post_load(void *opaque, int version_id)
{
    PIIX4PMState *s = opaque;

    pm_io_space_update(s);
    smbus_io_space_update(s);
    return 0;
}

#define VMSTATE_GPE_ARRAY(_field, _state)                            \
 {                                                                   \
     .name       = (stringify(_field)),                              \
     .version_id = 0,                                                \
     .info       = &vmstate_info_uint16,                             \
     .size       = sizeof(uint16_t),                                 \
     .flags      = VMS_SINGLE | VMS_POINTER,                         \
     .offset     = vmstate_offset_pointer(_state, _field, uint8_t),  \
 }

static const VMStateDescription vmstate_gpe = {
    .name = "gpe",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_GPE_ARRAY(sts, ACPIGPE),
        VMSTATE_GPE_ARRAY(en, ACPIGPE),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pci_status = {
    .name = "pci_status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(up, struct AcpiPciHpPciStatus),
        VMSTATE_UINT32(down, struct AcpiPciHpPciStatus),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_test_use_acpi_hotplug_bridge(void *opaque, int version_id)
{
    PIIX4PMState *s = opaque;
    return s->use_acpi_hotplug_bridge;
}

static bool vmstate_test_no_use_acpi_hotplug_bridge(void *opaque,
                                                    int version_id)
{
    PIIX4PMState *s = opaque;
    return !s->use_acpi_hotplug_bridge;
}

static bool vmstate_test_use_memhp(void *opaque)
{
    PIIX4PMState *s = opaque;
    return s->acpi_memory_hotplug.is_enabled;
}

static const VMStateDescription vmstate_memhp_state = {
    .name = "piix4_pm/memhp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_test_use_memhp,
    .fields      = (VMStateField[]) {
        VMSTATE_MEMORY_HOTPLUG(acpi_memory_hotplug, PIIX4PMState),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_test_use_cpuhp(void *opaque)
{
    PIIX4PMState *s = opaque;
    return !s->cpu_hotplug_legacy;
}

static int vmstate_cpuhp_pre_load(void *opaque)
{
    Object *obj = OBJECT(opaque);
    object_property_set_bool(obj, "cpu-hotplug-legacy", false, &error_abort);
    return 0;
}

static const VMStateDescription vmstate_cpuhp_state = {
    .name = "piix4_pm/cpuhp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_test_use_cpuhp,
    .pre_load = vmstate_cpuhp_pre_load,
    .fields      = (VMStateField[]) {
        VMSTATE_CPU_HOTPLUG(cpuhp_state, PIIX4PMState),
        VMSTATE_END_OF_LIST()
    }
};

static bool piix4_vmstate_need_smbus(void *opaque, int version_id)
{
    return pm_smbus_vmstate_needed();
}

/*
 * This is a fudge to turn off the acpi_index field,
 * whose test was always broken on piix4 with 6.2 and older machine types.
 */
static bool vmstate_test_migrate_acpi_index(void *opaque, int version_id)
{
    PIIX4PMState *s = PIIX4_PM(opaque);
    return s->use_acpi_hotplug_bridge && !s->not_migrate_acpi_index;
}

/* qemu-kvm 1.2 uses version 3 but advertised as 2
 * To support incoming qemu-kvm 1.2 migration, change version_id
 * and minimum_version_id to 2 below (which breaks migration from
 * qemu 1.2).
 *
 */
static const VMStateDescription vmstate_acpi = {
    .name = "piix4_pm",
    .version_id = 3,
    .minimum_version_id = 3,
    .post_load = vmstate_acpi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.evt.sts, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.evt.en, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.cnt.cnt, PIIX4PMState),
        VMSTATE_STRUCT(apm, PIIX4PMState, 0, vmstate_apm, APMState),
        VMSTATE_STRUCT_TEST(smb, PIIX4PMState, piix4_vmstate_need_smbus, 3,
                            pmsmb_vmstate, PMSMBus),
        VMSTATE_TIMER_PTR(ar.tmr.timer, PIIX4PMState),
        VMSTATE_INT64(ar.tmr.overflow_time, PIIX4PMState),
        VMSTATE_STRUCT(ar.gpe, PIIX4PMState, 2, vmstate_gpe, ACPIGPE),
        VMSTATE_STRUCT_TEST(
            acpi_pci_hotplug.acpi_pcihp_pci_status[ACPI_PCIHP_BSEL_DEFAULT],
            PIIX4PMState,
            vmstate_test_no_use_acpi_hotplug_bridge,
            2, vmstate_pci_status,
            struct AcpiPciHpPciStatus),
        VMSTATE_PCI_HOTPLUG(acpi_pci_hotplug, PIIX4PMState,
                            vmstate_test_use_acpi_hotplug_bridge,
                            vmstate_test_migrate_acpi_index),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
         &vmstate_memhp_state,
         &vmstate_cpuhp_state,
         NULL
    }
};

static void piix4_pm_reset(DeviceState *dev)
{
    PIIX4PMState *s = PIIX4_PM(dev);
    PCIDevice *d = PCI_DEVICE(s);
    uint8_t *pci_conf = d->config;

    pci_conf[0x58] = 0;
    pci_conf[0x59] = 0;
    pci_conf[0x5a] = 0;
    pci_conf[0x5b] = 0;

    pci_conf[0x40] = 0x01; /* PM io base read only bit */
    pci_conf[0x80] = 0;

    if (!s->smm_enabled) {
        /* Mark SMM as already inited (until KVM supports SMM). */
        pci_conf[0x5B] = 0x02;
    }

    acpi_pm1_evt_reset(&s->ar);
    acpi_pm1_cnt_reset(&s->ar);
    acpi_pm_tmr_reset(&s->ar);
    acpi_gpe_reset(&s->ar);
    acpi_update_sci(&s->ar, s->irq);

    pm_io_space_update(s);
    if (s->use_acpi_hotplug_bridge || s->use_acpi_root_pci_hotplug) {
        acpi_pcihp_reset(&s->acpi_pci_hotplug, !s->use_acpi_root_pci_hotplug);
    }
}

static void piix4_pm_powerdown_req(Notifier *n, void *opaque)
{
    PIIX4PMState *s = container_of(n, PIIX4PMState, powerdown_notifier);

    assert(s != NULL);
    acpi_pm1_evt_power_down(&s->ar);
}

static void piix4_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(hotplug_dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_pre_plug_cb(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        if (!s->acpi_memory_hotplug.is_enabled) {
            error_setg(errp,
                "memory hotplug is not enabled: %s.memory-hotplug-support "
                "is not set", object_get_typename(OBJECT(s)));
        }
    } else if (
               !object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        error_setg(errp, "acpi: device pre plug request for not supported"
                   " device type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void piix4_device_plug_cb(HotplugHandler *hotplug_dev,
                                 DeviceState *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(hotplug_dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        if (object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)) {
            nvdimm_acpi_plug_cb(hotplug_dev, dev);
        } else {
            acpi_memory_plug_cb(hotplug_dev, &s->acpi_memory_hotplug,
                                dev, errp);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_plug_cb(hotplug_dev, &s->acpi_pci_hotplug, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        if (s->cpu_hotplug_legacy) {
            legacy_acpi_cpu_plug_cb(hotplug_dev, &s->gpe_cpu, dev, errp);
        } else {
            acpi_cpu_plug_cb(hotplug_dev, &s->cpuhp_state, dev, errp);
        }
    } else {
        g_assert_not_reached();
    }
}

static void piix4_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                           DeviceState *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(hotplug_dev);

    if (s->acpi_memory_hotplug.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        acpi_memory_unplug_request_cb(hotplug_dev, &s->acpi_memory_hotplug,
                                      dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_unplug_request_cb(hotplug_dev, &s->acpi_pci_hotplug,
                                            dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU) &&
               !s->cpu_hotplug_legacy) {
        acpi_cpu_unplug_request_cb(hotplug_dev, &s->cpuhp_state, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void piix4_device_unplug_cb(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(hotplug_dev);

    if (s->acpi_memory_hotplug.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        acpi_memory_unplug_cb(&s->acpi_memory_hotplug, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_unplug_cb(hotplug_dev, &s->acpi_pci_hotplug, dev,
                                    errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU) &&
               !s->cpu_hotplug_legacy) {
        acpi_cpu_unplug_cb(&s->cpuhp_state, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void piix4_pm_machine_ready(Notifier *n, void *opaque)
{
    PIIX4PMState *s = container_of(n, PIIX4PMState, machine_ready);
    PCIDevice *d = PCI_DEVICE(s);
    MemoryRegion *io_as = pci_address_space_io(d);
    uint8_t *pci_conf;

    pci_conf = d->config;
    pci_conf[0x5f] = 0x10 |
        (memory_region_present(io_as, 0x378) ? 0x80 : 0);
    pci_conf[0x63] = 0x60;
    pci_conf[0x67] = (memory_region_present(io_as, 0x3f8) ? 0x08 : 0) |
        (memory_region_present(io_as, 0x2f8) ? 0x90 : 0);
}

static void piix4_pm_add_properties(PIIX4PMState *s)
{
    static const uint8_t acpi_enable_cmd = ACPI_ENABLE;
    static const uint8_t acpi_disable_cmd = ACPI_DISABLE;
    static const uint32_t gpe0_blk = GPE_BASE;
    static const uint32_t gpe0_blk_len = GPE_LEN;
    static const uint16_t sci_int = 9;

    object_property_add_uint8_ptr(OBJECT(s), ACPI_PM_PROP_ACPI_ENABLE_CMD,
                                  &acpi_enable_cmd, OBJ_PROP_FLAG_READ);
    object_property_add_uint8_ptr(OBJECT(s), ACPI_PM_PROP_ACPI_DISABLE_CMD,
                                  &acpi_disable_cmd, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_GPE0_BLK,
                                  &gpe0_blk, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_GPE0_BLK_LEN,
                                  &gpe0_blk_len, OBJ_PROP_FLAG_READ);
    object_property_add_uint16_ptr(OBJECT(s), ACPI_PM_PROP_SCI_INT,
                                  &sci_int, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_PM_IO_BASE,
                                  &s->io_base, OBJ_PROP_FLAG_READ);
}

static void piix4_pm_realize(PCIDevice *dev, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(dev);
    uint8_t *pci_conf;

    pci_conf = dev->config;
    pci_conf[0x06] = 0x80;
    pci_conf[0x07] = 0x02;
    pci_conf[0x09] = 0x00;
    pci_conf[0x3d] = 0x01; // interrupt pin 1

    /* APM */
    apm_init(dev, &s->apm, apm_ctrl_changed, s);

    if (!s->smm_enabled) {
        /* Mark SMM as already inited to prevent SMM from running.  KVM does not
         * support SMM mode. */
        pci_conf[0x5B] = 0x02;
    }

    /* XXX: which specification is used ? The i82731AB has different
       mappings */
    pci_conf[0x90] = s->smb_io_base | 1;
    pci_conf[0x91] = s->smb_io_base >> 8;
    pci_conf[0xd2] = 0x09;
    pm_smbus_init(DEVICE(dev), &s->smb, true);
    memory_region_set_enabled(&s->smb.io, pci_conf[0xd2] & 1);
    memory_region_add_subregion(pci_address_space_io(dev),
                                s->smb_io_base, &s->smb.io);

    memory_region_init(&s->io, OBJECT(s), "piix4-pm", 64);
    memory_region_set_enabled(&s->io, false);
    memory_region_add_subregion(pci_address_space_io(dev),
                                0, &s->io);

    acpi_pm_tmr_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_evt_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_cnt_init(&s->ar, &s->io, s->disable_s3, s->disable_s4, s->s4_val,
                      !s->smm_compat && !s->smm_enabled);
    acpi_gpe_init(&s->ar, GPE_LEN);

    s->powerdown_notifier.notify = piix4_pm_powerdown_req;
    qemu_register_powerdown_notifier(&s->powerdown_notifier);

    s->machine_ready.notify = piix4_pm_machine_ready;
    qemu_add_machine_init_done_notifier(&s->machine_ready);

    if (xen_enabled()) {
        s->use_acpi_hotplug_bridge = false;
    }

    piix4_acpi_system_hot_add_init(pci_address_space_io(dev),
                                   pci_get_bus(dev), s);
    qbus_set_hotplug_handler(BUS(pci_get_bus(dev)), OBJECT(s));

    piix4_pm_add_properties(s);
}

static void piix4_pm_init(Object *obj)
{
    PIIX4PMState *s = PIIX4_PM(obj);

    qdev_init_gpio_out(DEVICE(obj), &s->irq, 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->smi_irq, "smi-irq", 1);
}

static uint64_t gpe_readb(void *opaque, hwaddr addr, unsigned width)
{
    PIIX4PMState *s = opaque;
    uint32_t val = acpi_gpe_ioport_readb(&s->ar, addr);

    trace_piix4_gpe_readb(addr, width, val);
    return val;
}

static void gpe_writeb(void *opaque, hwaddr addr, uint64_t val,
                       unsigned width)
{
    PIIX4PMState *s = opaque;

    trace_piix4_gpe_writeb(addr, width, val);
    acpi_gpe_ioport_writeb(&s->ar, addr, val);
    acpi_update_sci(&s->ar, s->irq);
}

static const MemoryRegionOps piix4_gpe_ops = {
    .read = gpe_readb,
    .write = gpe_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static bool piix4_get_cpu_hotplug_legacy(Object *obj, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(obj);

    return s->cpu_hotplug_legacy;
}

static void piix4_set_cpu_hotplug_legacy(Object *obj, bool value, Error **errp)
{
    PIIX4PMState *s = PIIX4_PM(obj);

    assert(!value);
    if (s->cpu_hotplug_legacy && value == false) {
        acpi_switch_to_modern_cphp(&s->gpe_cpu, &s->cpuhp_state,
                                   PIIX4_CPU_HOTPLUG_IO_BASE);
    }
    s->cpu_hotplug_legacy = value;
}

static void piix4_acpi_system_hot_add_init(MemoryRegion *parent,
                                           PCIBus *bus, PIIX4PMState *s)
{
    memory_region_init_io(&s->io_gpe, OBJECT(s), &piix4_gpe_ops, s,
                          "acpi-gpe0", GPE_LEN);
    memory_region_add_subregion(parent, GPE_BASE, &s->io_gpe);

    if (s->use_acpi_hotplug_bridge || s->use_acpi_root_pci_hotplug) {
        acpi_pcihp_init(OBJECT(s), &s->acpi_pci_hotplug, bus, parent,
                        s->use_acpi_hotplug_bridge, ACPI_PCIHP_ADDR_PIIX4);
    }

    s->cpu_hotplug_legacy = true;
    object_property_add_bool(OBJECT(s), "cpu-hotplug-legacy",
                             piix4_get_cpu_hotplug_legacy,
                             piix4_set_cpu_hotplug_legacy);
    legacy_acpi_cpu_hotplug_init(parent, OBJECT(s), &s->gpe_cpu,
                                 PIIX4_CPU_HOTPLUG_IO_BASE);

    if (s->acpi_memory_hotplug.is_enabled) {
        acpi_memory_hotplug_init(parent, OBJECT(s), &s->acpi_memory_hotplug,
                                 ACPI_MEMORY_HOTPLUG_BASE);
    }
}

static void piix4_ospm_status(AcpiDeviceIf *adev, ACPIOSTInfoList ***list)
{
    PIIX4PMState *s = PIIX4_PM(adev);

    acpi_memory_ospm_status(&s->acpi_memory_hotplug, list);
    if (!s->cpu_hotplug_legacy) {
        acpi_cpu_ospm_status(&s->cpuhp_state, list);
    }
}

static void piix4_send_gpe(AcpiDeviceIf *adev, AcpiEventStatusBits ev)
{
    PIIX4PMState *s = PIIX4_PM(adev);

    acpi_send_gpe_event(&s->ar, s->irq, ev);
}

static Property piix4_pm_properties[] = {
    DEFINE_PROP_UINT32("smb_io_base", PIIX4PMState, smb_io_base, 0),
    DEFINE_PROP_UINT8(ACPI_PM_PROP_S3_DISABLED, PIIX4PMState, disable_s3, 0),
    DEFINE_PROP_UINT8(ACPI_PM_PROP_S4_DISABLED, PIIX4PMState, disable_s4, 0),
    DEFINE_PROP_UINT8(ACPI_PM_PROP_S4_VAL, PIIX4PMState, s4_val, 2),
    DEFINE_PROP_BOOL(ACPI_PM_PROP_ACPI_PCIHP_BRIDGE, PIIX4PMState,
                     use_acpi_hotplug_bridge, true),
    DEFINE_PROP_BOOL(ACPI_PM_PROP_ACPI_PCI_ROOTHP, PIIX4PMState,
                     use_acpi_root_pci_hotplug, true),
    DEFINE_PROP_BOOL("memory-hotplug-support", PIIX4PMState,
                     acpi_memory_hotplug.is_enabled, true),
    DEFINE_PROP_BOOL("smm-compat", PIIX4PMState, smm_compat, false),
    DEFINE_PROP_BOOL("smm-enabled", PIIX4PMState, smm_enabled, false),
    DEFINE_PROP_BOOL("x-not-migrate-acpi-index", PIIX4PMState,
                      not_migrate_acpi_index, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void piix4_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(klass);

    k->realize = piix4_pm_realize;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_3;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    dc->reset = piix4_pm_reset;
    dc->desc = "PM";
    dc->vmsd = &vmstate_acpi;
    device_class_set_props(dc, piix4_pm_properties);
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->user_creatable = false;
    dc->hotpluggable = false;
    hc->pre_plug = piix4_device_pre_plug_cb;
    hc->plug = piix4_device_plug_cb;
    hc->unplug_request = piix4_device_unplug_request_cb;
    hc->unplug = piix4_device_unplug_cb;
    adevc->ospm_status = piix4_ospm_status;
    adevc->send_event = piix4_send_gpe;
    adevc->madt_cpu = pc_madt_cpu_entry;
}

static const TypeInfo piix4_pm_info = {
    .name          = TYPE_PIIX4_PM,
    .parent        = TYPE_PCI_DEVICE,
    .instance_init  = piix4_pm_init,
    .instance_size = sizeof(PIIX4PMState),
    .class_init    = piix4_pm_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void piix4_pm_register_types(void)
{
    type_register_static(&piix4_pm_info);
}

type_init(piix4_pm_register_types)
