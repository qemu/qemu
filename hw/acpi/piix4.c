/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "hw/i386/pc.h"
#include "hw/isa/apm.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/pci/pci.h"
#include "hw/acpi/acpi.h"
#include "sysemu/sysemu.h"
#include "qemu/range.h"
#include "exec/ioport.h"
#include "hw/nvram/fw_cfg.h"
#include "exec/address-spaces.h"

//#define DEBUG

#ifdef DEBUG
# define PIIX4_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define PIIX4_DPRINTF(format, ...)     do { } while (0)
#endif

#define GPE_BASE 0xafe0
#define GPE_LEN 4

#define PCI_HOTPLUG_ADDR 0xae00
#define PCI_HOTPLUG_SIZE 0x000f
#define PCI_UP_BASE 0xae00
#define PCI_DOWN_BASE 0xae04
#define PCI_EJ_BASE 0xae08
#define PCI_RMV_BASE 0xae0c

#define PIIX4_PROC_BASE 0xaf00
#define PIIX4_PROC_LEN 32

#define PIIX4_PCI_HOTPLUG_STATUS 2
#define PIIX4_CPU_HOTPLUG_STATUS 4

struct pci_status {
    uint32_t up; /* deprecated, maintained for migration compatibility */
    uint32_t down;
};

typedef struct CPUStatus {
    uint8_t sts[PIIX4_PROC_LEN];
} CPUStatus;

typedef struct PIIX4PMState {
    PCIDevice dev;

    MemoryRegion io;
    MemoryRegion io_gpe;
    MemoryRegion io_pci;
    MemoryRegion io_cpu;
    ACPIREGS ar;

    APMState apm;

    PMSMBus smb;
    uint32_t smb_io_base;

    qemu_irq irq;
    qemu_irq smi_irq;
    int kvm_enabled;
    Notifier machine_ready;
    Notifier powerdown_notifier;

    /* for pci hotplug */
    struct pci_status pci0_status;
    uint32_t pci0_hotplug_enable;
    uint32_t pci0_slot_device_present;

    uint8_t disable_s3;
    uint8_t disable_s4;
    uint8_t s4_val;

    CPUStatus gpe_cpu;
    Notifier cpu_added_notifier;
} PIIX4PMState;

static void piix4_acpi_system_hot_add_init(MemoryRegion *parent,
                                           PCIBus *bus, PIIX4PMState *s);

#define ACPI_ENABLE 0xf1
#define ACPI_DISABLE 0xf0

static void pm_update_sci(PIIX4PMState *s)
{
    int sci_level, pmsts;

    pmsts = acpi_pm1_evt_get_sts(&s->ar);
    sci_level = (((pmsts & s->ar.pm1.evt.en) &
                  (ACPI_BITMASK_RT_CLOCK_ENABLE |
                   ACPI_BITMASK_POWER_BUTTON_ENABLE |
                   ACPI_BITMASK_GLOBAL_LOCK_ENABLE |
                   ACPI_BITMASK_TIMER_ENABLE)) != 0) ||
        (((s->ar.gpe.sts[0] & s->ar.gpe.en[0]) &
          (PIIX4_PCI_HOTPLUG_STATUS | PIIX4_CPU_HOTPLUG_STATUS)) != 0);

    qemu_set_irq(s->irq, sci_level);
    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(&s->ar, (s->ar.pm1.evt.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pmsts & ACPI_BITMASK_TIMER_STATUS));
}

static void pm_tmr_timer(ACPIREGS *ar)
{
    PIIX4PMState *s = container_of(ar, PIIX4PMState, ar);
    pm_update_sci(s);
}

static void apm_ctrl_changed(uint32_t val, void *arg)
{
    PIIX4PMState *s = arg;

    /* ACPI specs 3.0, 4.7.2.5 */
    acpi_pm1_cnt_update(&s->ar, val == ACPI_ENABLE, val == ACPI_DISABLE);

    if (s->dev.config[0x5b] & (1 << 1)) {
        if (s->smi_irq) {
            qemu_irq_raise(s->smi_irq);
        }
    }
}

static void pm_io_space_update(PIIX4PMState *s)
{
    uint32_t pm_io_base;

    pm_io_base = le32_to_cpu(*(uint32_t *)(s->dev.config + 0x40));
    pm_io_base &= 0xffc0;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->io, s->dev.config[0x80] & 1);
    memory_region_set_address(&s->io, pm_io_base);
    memory_region_transaction_commit();
}

static void smbus_io_space_update(PIIX4PMState *s)
{
    s->smb_io_base = le32_to_cpu(*(uint32_t *)(s->dev.config + 0x90));
    s->smb_io_base &= 0xffc0;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->smb.io, s->dev.config[0xd2] & 1);
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

static void vmstate_pci_status_pre_save(void *opaque)
{
    struct pci_status *pci0_status = opaque;
    PIIX4PMState *s = container_of(pci0_status, PIIX4PMState, pci0_status);

    /* We no longer track up, so build a safe value for migrating
     * to a version that still does... of course these might get lost
     * by an old buggy implementation, but we try. */
    pci0_status->up = s->pci0_slot_device_present & s->pci0_hotplug_enable;
}

static int vmstate_acpi_post_load(void *opaque, int version_id)
{
    PIIX4PMState *s = opaque;

    pm_io_space_update(s);
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
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_GPE_ARRAY(sts, ACPIGPE),
        VMSTATE_GPE_ARRAY(en, ACPIGPE),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pci_status = {
    .name = "pci_status",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = vmstate_pci_status_pre_save,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(up, struct pci_status),
        VMSTATE_UINT32(down, struct pci_status),
        VMSTATE_END_OF_LIST()
    }
};

static int acpi_load_old(QEMUFile *f, void *opaque, int version_id)
{
    PIIX4PMState *s = opaque;
    int ret, i;
    uint16_t temp;

    ret = pci_device_load(&s->dev, f);
    if (ret < 0) {
        return ret;
    }
    qemu_get_be16s(f, &s->ar.pm1.evt.sts);
    qemu_get_be16s(f, &s->ar.pm1.evt.en);
    qemu_get_be16s(f, &s->ar.pm1.cnt.cnt);

    ret = vmstate_load_state(f, &vmstate_apm, &s->apm, 1);
    if (ret) {
        return ret;
    }

    qemu_get_timer(f, s->ar.tmr.timer);
    qemu_get_sbe64s(f, &s->ar.tmr.overflow_time);

    qemu_get_be16s(f, (uint16_t *)s->ar.gpe.sts);
    for (i = 0; i < 3; i++) {
        qemu_get_be16s(f, &temp);
    }

    qemu_get_be16s(f, (uint16_t *)s->ar.gpe.en);
    for (i = 0; i < 3; i++) {
        qemu_get_be16s(f, &temp);
    }

    ret = vmstate_load_state(f, &vmstate_pci_status, &s->pci0_status, 1);
    return ret;
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
    .minimum_version_id_old = 1,
    .load_state_old = acpi_load_old,
    .post_load = vmstate_acpi_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.evt.sts, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.evt.en, PIIX4PMState),
        VMSTATE_UINT16(ar.pm1.cnt.cnt, PIIX4PMState),
        VMSTATE_STRUCT(apm, PIIX4PMState, 0, vmstate_apm, APMState),
        VMSTATE_TIMER(ar.tmr.timer, PIIX4PMState),
        VMSTATE_INT64(ar.tmr.overflow_time, PIIX4PMState),
        VMSTATE_STRUCT(ar.gpe, PIIX4PMState, 2, vmstate_gpe, ACPIGPE),
        VMSTATE_STRUCT(pci0_status, PIIX4PMState, 2, vmstate_pci_status,
                       struct pci_status),
        VMSTATE_END_OF_LIST()
    }
};

static void acpi_piix_eject_slot(PIIX4PMState *s, unsigned slots)
{
    BusChild *kid, *next;
    BusState *bus = qdev_get_parent_bus(&s->dev.qdev);
    int slot = ffs(slots) - 1;
    bool slot_free = true;

    /* Mark request as complete */
    s->pci0_status.down &= ~(1U << slot);

    QTAILQ_FOREACH_SAFE(kid, &bus->children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *dev = PCI_DEVICE(qdev);
        PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
        if (PCI_SLOT(dev->devfn) == slot) {
            if (pc->no_hotplug) {
                slot_free = false;
            } else {
                qdev_free(qdev);
            }
        }
    }
    if (slot_free) {
        s->pci0_slot_device_present &= ~(1U << slot);
    }
}

static void piix4_update_hotplug(PIIX4PMState *s)
{
    PCIDevice *dev = &s->dev;
    BusState *bus = qdev_get_parent_bus(&dev->qdev);
    BusChild *kid, *next;

    /* Execute any pending removes during reset */
    while (s->pci0_status.down) {
        acpi_piix_eject_slot(s, s->pci0_status.down);
    }

    s->pci0_hotplug_enable = ~0;
    s->pci0_slot_device_present = 0;

    QTAILQ_FOREACH_SAFE(kid, &bus->children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *pdev = PCI_DEVICE(qdev);
        PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pdev);
        int slot = PCI_SLOT(pdev->devfn);

        if (pc->no_hotplug) {
            s->pci0_hotplug_enable &= ~(1U << slot);
        }

        s->pci0_slot_device_present |= (1U << slot);
    }
}

static void piix4_reset(void *opaque)
{
    PIIX4PMState *s = opaque;
    uint8_t *pci_conf = s->dev.config;

    pci_conf[0x58] = 0;
    pci_conf[0x59] = 0;
    pci_conf[0x5a] = 0;
    pci_conf[0x5b] = 0;

    pci_conf[0x40] = 0x01; /* PM io base read only bit */
    pci_conf[0x80] = 0;

    if (s->kvm_enabled) {
        /* Mark SMM as already inited (until KVM supports SMM). */
        pci_conf[0x5B] = 0x02;
    }
    piix4_update_hotplug(s);
}

static void piix4_pm_powerdown_req(Notifier *n, void *opaque)
{
    PIIX4PMState *s = container_of(n, PIIX4PMState, powerdown_notifier);

    assert(s != NULL);
    acpi_pm1_evt_power_down(&s->ar);
}

static void piix4_pm_machine_ready(Notifier *n, void *opaque)
{
    PIIX4PMState *s = container_of(n, PIIX4PMState, machine_ready);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_conf[0x5f] = (isa_is_ioport_assigned(0x378) ? 0x80 : 0) | 0x10;
    pci_conf[0x63] = 0x60;
    pci_conf[0x67] = (isa_is_ioport_assigned(0x3f8) ? 0x08 : 0) |
	(isa_is_ioport_assigned(0x2f8) ? 0x90 : 0);

}

static int piix4_pm_initfn(PCIDevice *dev)
{
    PIIX4PMState *s = DO_UPCAST(PIIX4PMState, dev, dev);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_conf[0x06] = 0x80;
    pci_conf[0x07] = 0x02;
    pci_conf[0x09] = 0x00;
    pci_conf[0x3d] = 0x01; // interrupt pin 1

    /* APM */
    apm_init(dev, &s->apm, apm_ctrl_changed, s);

    if (s->kvm_enabled) {
        /* Mark SMM as already inited to prevent SMM from running.  KVM does not
         * support SMM mode. */
        pci_conf[0x5B] = 0x02;
    }

    /* XXX: which specification is used ? The i82731AB has different
       mappings */
    pci_conf[0x90] = s->smb_io_base | 1;
    pci_conf[0x91] = s->smb_io_base >> 8;
    pci_conf[0xd2] = 0x09;
    pm_smbus_init(&s->dev.qdev, &s->smb);
    memory_region_set_enabled(&s->smb.io, pci_conf[0xd2] & 1);
    memory_region_add_subregion(pci_address_space_io(dev),
                                s->smb_io_base, &s->smb.io);

    memory_region_init(&s->io, "piix4-pm", 64);
    memory_region_set_enabled(&s->io, false);
    memory_region_add_subregion(pci_address_space_io(dev),
                                0, &s->io);

    acpi_pm_tmr_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_evt_init(&s->ar, pm_tmr_timer, &s->io);
    acpi_pm1_cnt_init(&s->ar, &s->io, s->s4_val);
    acpi_gpe_init(&s->ar, GPE_LEN);

    s->powerdown_notifier.notify = piix4_pm_powerdown_req;
    qemu_register_powerdown_notifier(&s->powerdown_notifier);

    s->machine_ready.notify = piix4_pm_machine_ready;
    qemu_add_machine_init_done_notifier(&s->machine_ready);
    qemu_register_reset(piix4_reset, s);

    piix4_acpi_system_hot_add_init(pci_address_space_io(dev), dev->bus, s);

    return 0;
}

i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq, qemu_irq smi_irq,
                       int kvm_enabled, void *fw_cfg)
{
    PCIDevice *dev;
    PIIX4PMState *s;

    dev = pci_create(bus, devfn, "PIIX4_PM");
    qdev_prop_set_uint32(&dev->qdev, "smb_io_base", smb_io_base);

    s = DO_UPCAST(PIIX4PMState, dev, dev);
    s->irq = sci_irq;
    s->smi_irq = smi_irq;
    s->kvm_enabled = kvm_enabled;

    qdev_init_nofail(&dev->qdev);

    if (fw_cfg) {
        uint8_t suspend[6] = {128, 0, 0, 129, 128, 128};
        suspend[3] = 1 | ((!s->disable_s3) << 7);
        suspend[4] = s->s4_val | ((!s->disable_s4) << 7);

        fw_cfg_add_file(fw_cfg, "etc/system-states", g_memdup(suspend, 6), 6);
    }

    return s->smb.smbus;
}

static Property piix4_pm_properties[] = {
    DEFINE_PROP_UINT32("smb_io_base", PIIX4PMState, smb_io_base, 0),
    DEFINE_PROP_UINT8("disable_s3", PIIX4PMState, disable_s3, 0),
    DEFINE_PROP_UINT8("disable_s4", PIIX4PMState, disable_s4, 0),
    DEFINE_PROP_UINT8("s4_val", PIIX4PMState, s4_val, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void piix4_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug = 1;
    k->init = piix4_pm_initfn;
    k->config_write = pm_write_config;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_3;
    k->revision = 0x03;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    dc->desc = "PM";
    dc->no_user = 1;
    dc->vmsd = &vmstate_acpi;
    dc->props = piix4_pm_properties;
}

static const TypeInfo piix4_pm_info = {
    .name          = "PIIX4_PM",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX4PMState),
    .class_init    = piix4_pm_class_init,
};

static void piix4_pm_register_types(void)
{
    type_register_static(&piix4_pm_info);
}

type_init(piix4_pm_register_types)

static uint64_t gpe_readb(void *opaque, hwaddr addr, unsigned width)
{
    PIIX4PMState *s = opaque;
    uint32_t val = acpi_gpe_ioport_readb(&s->ar, addr);

    PIIX4_DPRINTF("gpe read %x == %x\n", addr, val);
    return val;
}

static void gpe_writeb(void *opaque, hwaddr addr, uint64_t val,
                       unsigned width)
{
    PIIX4PMState *s = opaque;

    acpi_gpe_ioport_writeb(&s->ar, addr, val);
    pm_update_sci(s);

    PIIX4_DPRINTF("gpe write %x <== %d\n", addr, val);
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

static uint64_t pci_read(void *opaque, hwaddr addr, unsigned int size)
{
    PIIX4PMState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case PCI_UP_BASE - PCI_HOTPLUG_ADDR:
        /* Manufacture an "up" value to cause a device check on any hotplug
         * slot with a device.  Extra device checks are harmless. */
        val = s->pci0_slot_device_present & s->pci0_hotplug_enable;
        PIIX4_DPRINTF("pci_up_read %x\n", val);
        break;
    case PCI_DOWN_BASE - PCI_HOTPLUG_ADDR:
        val = s->pci0_status.down;
        PIIX4_DPRINTF("pci_down_read %x\n", val);
        break;
    case PCI_EJ_BASE - PCI_HOTPLUG_ADDR:
        /* No feature defined yet */
        PIIX4_DPRINTF("pci_features_read %x\n", val);
        break;
    case PCI_RMV_BASE - PCI_HOTPLUG_ADDR:
        val = s->pci0_hotplug_enable;
        break;
    default:
        break;
    }

    return val;
}

static void pci_write(void *opaque, hwaddr addr, uint64_t data,
                      unsigned int size)
{
    switch (addr) {
    case PCI_EJ_BASE - PCI_HOTPLUG_ADDR:
        acpi_piix_eject_slot(opaque, (uint32_t)data);
        PIIX4_DPRINTF("pciej write %" HWADDR_PRIx " <== % " PRIu64 "\n",
                      addr, data);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps piix4_pci_ops = {
    .read = pci_read,
    .write = pci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t cpu_status_read(void *opaque, hwaddr addr, unsigned int size)
{
    PIIX4PMState *s = opaque;
    CPUStatus *cpus = &s->gpe_cpu;
    uint64_t val = cpus->sts[addr];

    return val;
}

static void cpu_status_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned int size)
{
    /* TODO: implement VCPU removal on guest signal that CPU can be removed */
}

static const MemoryRegionOps cpu_hotplug_ops = {
    .read = cpu_status_read,
    .write = cpu_status_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

typedef enum {
    PLUG,
    UNPLUG,
} HotplugEventType;

static void piix4_cpu_hotplug_req(PIIX4PMState *s, CPUState *cpu,
                                  HotplugEventType action)
{
    CPUStatus *g = &s->gpe_cpu;
    ACPIGPE *gpe = &s->ar.gpe;
    CPUClass *k = CPU_GET_CLASS(cpu);
    int64_t cpu_id;

    assert(s != NULL);

    *gpe->sts = *gpe->sts | PIIX4_CPU_HOTPLUG_STATUS;
    cpu_id = k->get_arch_id(CPU(cpu));
    if (action == PLUG) {
        g->sts[cpu_id / 8] |= (1 << (cpu_id % 8));
    } else {
        g->sts[cpu_id / 8] &= ~(1 << (cpu_id % 8));
    }
    pm_update_sci(s);
}

static void piix4_cpu_added_req(Notifier *n, void *opaque)
{
    PIIX4PMState *s = container_of(n, PIIX4PMState, cpu_added_notifier);

    piix4_cpu_hotplug_req(s, CPU(opaque), PLUG);
}

static void piix4_init_cpu_status(CPUState *cpu, void *data)
{
    CPUStatus *g = (CPUStatus *)data;
    CPUClass *k = CPU_GET_CLASS(cpu);
    int64_t id = k->get_arch_id(cpu);

    g_assert((id / 8) < PIIX4_PROC_LEN);
    g->sts[id / 8] |= (1 << (id % 8));
}

static int piix4_device_hotplug(DeviceState *qdev, PCIDevice *dev,
                                PCIHotplugState state);

static void piix4_acpi_system_hot_add_init(MemoryRegion *parent,
                                           PCIBus *bus, PIIX4PMState *s)
{
    memory_region_init_io(&s->io_gpe, &piix4_gpe_ops, s, "apci-gpe0",
                          GPE_LEN);
    memory_region_add_subregion(parent, GPE_BASE, &s->io_gpe);

    memory_region_init_io(&s->io_pci, &piix4_pci_ops, s, "apci-pci-hotplug",
                          PCI_HOTPLUG_SIZE);
    memory_region_add_subregion(parent, PCI_HOTPLUG_ADDR,
                                &s->io_pci);
    pci_bus_hotplug(bus, piix4_device_hotplug, &s->dev.qdev);

    qemu_for_each_cpu(piix4_init_cpu_status, &s->gpe_cpu);
    memory_region_init_io(&s->io_cpu, &cpu_hotplug_ops, s, "apci-cpu-hotplug",
                          PIIX4_PROC_LEN);
    memory_region_add_subregion(parent, PIIX4_PROC_BASE, &s->io_cpu);
    s->cpu_added_notifier.notify = piix4_cpu_added_req;
    qemu_register_cpu_added_notifier(&s->cpu_added_notifier);
}

static void enable_device(PIIX4PMState *s, int slot)
{
    s->ar.gpe.sts[0] |= PIIX4_PCI_HOTPLUG_STATUS;
    s->pci0_slot_device_present |= (1U << slot);
}

static void disable_device(PIIX4PMState *s, int slot)
{
    s->ar.gpe.sts[0] |= PIIX4_PCI_HOTPLUG_STATUS;
    s->pci0_status.down |= (1U << slot);
}

static int piix4_device_hotplug(DeviceState *qdev, PCIDevice *dev,
				PCIHotplugState state)
{
    int slot = PCI_SLOT(dev->devfn);
    PIIX4PMState *s = DO_UPCAST(PIIX4PMState, dev,
                                PCI_DEVICE(qdev));

    /* Don't send event when device is enabled during qemu machine creation:
     * it is present on boot, no hotplug event is necessary. We do send an
     * event when the device is disabled later. */
    if (state == PCI_COLDPLUG_ENABLED) {
        s->pci0_slot_device_present |= (1U << slot);
        return 0;
    }

    if (state == PCI_HOTPLUG_ENABLED) {
        enable_device(s, slot);
    } else {
        disable_device(s, slot);
    }

    pm_update_sci(s);

    return 0;
}
