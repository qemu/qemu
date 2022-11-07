/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qapi-visit-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "acpi-microvm.h"
#include "microvm-dt.h"

#include "hw/loader.h"
#include "hw/irq.h"
#include "hw/kvm/clock.h"
#include "hw/i386/microvm.h"
#include "hw/i386/x86.h"
#include "target/i386/cpu.h"
#include "hw/intc/i8259.h"
#include "hw/timer/i8254.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/char/serial.h"
#include "hw/display/ramfb.h"
#include "hw/i386/topology.h"
#include "hw/i386/e820_memory_layout.h"
#include "hw/i386/fw_cfg.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/pci-host/gpex.h"
#include "hw/usb/xhci.h"

#include "elf.h"
#include "kvm/kvm_i386.h"
#include "hw/xen/start_info.h"

#define MICROVM_QBOOT_FILENAME "qboot.rom"
#define MICROVM_BIOS_FILENAME  "bios-microvm.bin"

static void microvm_set_rtc(MicrovmMachineState *mms, ISADevice *s)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    int val;

    val = MIN(x86ms->below_4g_mem_size / KiB, 640);
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);
    /* extended memory (next 64MiB) */
    if (x86ms->below_4g_mem_size > 1 * MiB) {
        val = (x86ms->below_4g_mem_size - 1 * MiB) / KiB;
    } else {
        val = 0;
    }
    if (val > 65535) {
        val = 65535;
    }
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);
    /* memory between 16MiB and 4GiB */
    if (x86ms->below_4g_mem_size > 16 * MiB) {
        val = (x86ms->below_4g_mem_size - 16 * MiB) / (64 * KiB);
    } else {
        val = 0;
    }
    if (val > 65535) {
        val = 65535;
    }
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);
    /* memory above 4GiB */
    val = x86ms->above_4g_mem_size / 65536;
    rtc_set_memory(s, 0x5b, val);
    rtc_set_memory(s, 0x5c, val >> 8);
    rtc_set_memory(s, 0x5d, val >> 16);
}

static void create_gpex(MicrovmMachineState *mms)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    MemoryRegion *mmio32_alias;
    MemoryRegion *mmio64_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    DeviceState *dev;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Map only the first size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, mms->gpex.ecam.size);
    memory_region_add_subregion(get_system_memory(),
                                mms->gpex.ecam.base, ecam_alias);

    /* Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    if (mms->gpex.mmio32.size) {
        mmio32_alias = g_new0(MemoryRegion, 1);
        memory_region_init_alias(mmio32_alias, OBJECT(dev), "pcie-mmio32", mmio_reg,
                                 mms->gpex.mmio32.base, mms->gpex.mmio32.size);
        memory_region_add_subregion(get_system_memory(),
                                    mms->gpex.mmio32.base, mmio32_alias);
    }
    if (mms->gpex.mmio64.size) {
        mmio64_alias = g_new0(MemoryRegion, 1);
        memory_region_init_alias(mmio64_alias, OBJECT(dev), "pcie-mmio64", mmio_reg,
                                 mms->gpex.mmio64.base, mms->gpex.mmio64.size);
        memory_region_add_subregion(get_system_memory(),
                                    mms->gpex.mmio64.base, mmio64_alias);
    }

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           x86ms->gsi[mms->gpex.irq + i]);
    }
}

static int microvm_ioapics(MicrovmMachineState *mms)
{
    if (!x86_machine_is_acpi_enabled(X86_MACHINE(mms))) {
        return 1;
    }
    if (mms->ioapic2 == ON_OFF_AUTO_OFF) {
        return 1;
    }
    return 2;
}

static void microvm_devices_init(MicrovmMachineState *mms)
{
    const char *default_firmware;
    X86MachineState *x86ms = X86_MACHINE(mms);
    ISABus *isa_bus;
    ISADevice *rtc_state;
    GSIState *gsi_state;
    int ioapics;
    int i;

    /* Core components */
    ioapics = microvm_ioapics(mms);
    gsi_state = g_malloc0(sizeof(*gsi_state));
    x86ms->gsi = qemu_allocate_irqs(gsi_handler, gsi_state,
                                    IOAPIC_NUM_PINS * ioapics);

    isa_bus = isa_bus_new(NULL, get_system_memory(), get_system_io(),
                          &error_abort);
    isa_bus_irqs(isa_bus, x86ms->gsi);

    ioapic_init_gsi(gsi_state, "machine");
    if (ioapics > 1) {
        x86ms->ioapic2 = ioapic_init_secondary(gsi_state);
    }

    kvmclock_create(true);

    mms->virtio_irq_base = 5;
    mms->virtio_num_transports = 8;
    if (x86ms->ioapic2) {
        mms->pcie_irq_base = 16;    /* 16 -> 19 */
        /* use second ioapic (24 -> 47) for virtio-mmio irq lines */
        mms->virtio_irq_base = IO_APIC_SECONDARY_IRQBASE;
        mms->virtio_num_transports = IOAPIC_NUM_PINS;
    } else if (x86_machine_is_acpi_enabled(x86ms)) {
        mms->pcie_irq_base = 12;    /* 12 -> 15 */
        mms->virtio_irq_base = 16;  /* 16 -> 23 */
    }

    for (i = 0; i < mms->virtio_num_transports; i++) {
        sysbus_create_simple("virtio-mmio",
                             VIRTIO_MMIO_BASE + i * 512,
                             x86ms->gsi[mms->virtio_irq_base + i]);
    }

    /* Optional and legacy devices */
    if (x86_machine_is_acpi_enabled(x86ms)) {
        DeviceState *dev = qdev_new(TYPE_ACPI_GED_X86);
        qdev_prop_set_uint32(dev, "ged-event", ACPI_GED_PWR_DOWN_EVT);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, GED_MMIO_BASE);
        /* sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, GED_MMIO_BASE_MEMHP); */
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, GED_MMIO_BASE_REGS);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           x86ms->gsi[GED_MMIO_IRQ]);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);
        x86ms->acpi_dev = HOTPLUG_HANDLER(dev);
    }

    if (x86_machine_is_acpi_enabled(x86ms) && machine_usb(MACHINE(mms))) {
        DeviceState *dev = qdev_new(TYPE_XHCI_SYSBUS);
        qdev_prop_set_uint32(dev, "intrs", 1);
        qdev_prop_set_uint32(dev, "slots", XHCI_MAXSLOTS);
        qdev_prop_set_uint32(dev, "p2", 8);
        qdev_prop_set_uint32(dev, "p3", 8);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, MICROVM_XHCI_BASE);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           x86ms->gsi[MICROVM_XHCI_IRQ]);
    }

    if (x86_machine_is_acpi_enabled(x86ms) && mms->pcie == ON_OFF_AUTO_ON) {
        /* use topmost 25% of the address space available */
        hwaddr phys_size = (hwaddr)1 << X86_CPU(first_cpu)->phys_bits;
        if (phys_size > 0x1000000ll) {
            mms->gpex.mmio64.size = phys_size / 4;
            mms->gpex.mmio64.base = phys_size - mms->gpex.mmio64.size;
        }
        mms->gpex.mmio32.base = PCIE_MMIO_BASE;
        mms->gpex.mmio32.size = PCIE_MMIO_SIZE;
        mms->gpex.ecam.base   = PCIE_ECAM_BASE;
        mms->gpex.ecam.size   = PCIE_ECAM_SIZE;
        mms->gpex.irq         = mms->pcie_irq_base;
        create_gpex(mms);
        x86ms->pci_irq_mask = ((1 << (mms->pcie_irq_base + 0)) |
                               (1 << (mms->pcie_irq_base + 1)) |
                               (1 << (mms->pcie_irq_base + 2)) |
                               (1 << (mms->pcie_irq_base + 3)));
    } else {
        x86ms->pci_irq_mask = 0;
    }

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        qemu_irq *i8259;

        i8259 = i8259_init(isa_bus, x86_allocate_cpu_irq());
        for (i = 0; i < ISA_NUM_IRQS; i++) {
            gsi_state->i8259_irq[i] = i8259[i];
        }
        g_free(i8259);
    }

    if (x86ms->pit == ON_OFF_AUTO_ON || x86ms->pit == ON_OFF_AUTO_AUTO) {
        if (kvm_pit_in_kernel()) {
            kvm_pit_init(isa_bus, 0x40);
        } else {
            i8254_pit_init(isa_bus, 0x40, 0, NULL);
        }
    }

    if (mms->rtc == ON_OFF_AUTO_ON ||
        (mms->rtc == ON_OFF_AUTO_AUTO && !kvm_enabled())) {
        rtc_state = mc146818_rtc_init(isa_bus, 2000, NULL);
        microvm_set_rtc(mms, rtc_state);
    }

    if (mms->isa_serial) {
        serial_hds_isa_init(isa_bus, 0, 1);
    }

    default_firmware = x86_machine_is_acpi_enabled(x86ms)
            ? MICROVM_BIOS_FILENAME
            : MICROVM_QBOOT_FILENAME;
    x86_bios_rom_init(MACHINE(mms), default_firmware, get_system_memory(), true);
}

static void microvm_memory_init(MicrovmMachineState *mms)
{
    MachineState *machine = MACHINE(mms);
    X86MachineState *x86ms = X86_MACHINE(mms);
    MemoryRegion *ram_below_4g, *ram_above_4g;
    MemoryRegion *system_memory = get_system_memory();
    FWCfgState *fw_cfg;
    ram_addr_t lowmem = 0xc0000000; /* 3G */
    int i;

    if (machine->ram_size > lowmem) {
        x86ms->above_4g_mem_size = machine->ram_size - lowmem;
        x86ms->below_4g_mem_size = lowmem;
    } else {
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = machine->ram_size;
    }

    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", machine->ram,
                             0, x86ms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);

    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM);

    if (x86ms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g",
                                 machine->ram,
                                 x86ms->below_4g_mem_size,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, x86ms->above_4g_mem_size, E820_RAM);
    }

    fw_cfg = fw_cfg_init_io_dma(FW_CFG_IO_BASE, FW_CFG_IO_BASE + 4,
                                &address_space_memory);

    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, machine->smp.cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, machine->smp.max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i32(fw_cfg, FW_CFG_IRQ0_OVERRIDE, 1);
    fw_cfg_add_file(fw_cfg, "etc/e820", e820_table,
                    sizeof(struct e820_entry) * e820_get_num_entries());

    rom_set_fw(fw_cfg);

    if (machine->kernel_filename != NULL) {
        x86_load_linux(x86ms, fw_cfg, 0, true, false);
    }

    if (mms->option_roms) {
        for (i = 0; i < nb_option_roms; i++) {
            rom_add_option(option_rom[i].name, option_rom[i].bootindex);
        }
    }

    x86ms->fw_cfg = fw_cfg;
    x86ms->ioapic_as = &address_space_memory;
}

static gchar *microvm_get_mmio_cmdline(gchar *name, uint32_t virtio_irq_base)
{
    gchar *cmdline;
    gchar *separator;
    long int index;
    int ret;

    separator = g_strrstr(name, ".");
    if (!separator) {
        return NULL;
    }

    if (qemu_strtol(separator + 1, NULL, 10, &index) != 0) {
        return NULL;
    }

    cmdline = g_malloc0(VIRTIO_CMDLINE_MAXLEN);
    ret = g_snprintf(cmdline, VIRTIO_CMDLINE_MAXLEN,
                     " virtio_mmio.device=512@0x%lx:%ld",
                     VIRTIO_MMIO_BASE + index * 512,
                     virtio_irq_base + index);
    if (ret < 0 || ret >= VIRTIO_CMDLINE_MAXLEN) {
        g_free(cmdline);
        return NULL;
    }

    return cmdline;
}

static void microvm_fix_kernel_cmdline(MachineState *machine)
{
    X86MachineState *x86ms = X86_MACHINE(machine);
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    BusState *bus;
    BusChild *kid;
    char *cmdline;

    /*
     * Find MMIO transports with attached devices, and add them to the kernel
     * command line.
     *
     * Yes, this is a hack, but one that heavily improves the UX without
     * introducing any significant issues.
     */
    cmdline = g_strdup(machine->kernel_cmdline);
    bus = sysbus_get_default();
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        ObjectClass *class = object_get_class(OBJECT(dev));

        if (class == object_class_by_name(TYPE_VIRTIO_MMIO)) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(OBJECT(dev));
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;

            if (!QTAILQ_EMPTY(&mmio_bus->children)) {
                gchar *mmio_cmdline = microvm_get_mmio_cmdline
                    (mmio_bus->name, mms->virtio_irq_base);
                if (mmio_cmdline) {
                    char *newcmd = g_strjoin(NULL, cmdline, mmio_cmdline, NULL);
                    g_free(mmio_cmdline);
                    g_free(cmdline);
                    cmdline = newcmd;
                }
            }
        }
    }

    fw_cfg_modify_i32(x86ms->fw_cfg, FW_CFG_CMDLINE_SIZE, strlen(cmdline) + 1);
    fw_cfg_modify_string(x86ms->fw_cfg, FW_CFG_CMDLINE_DATA, cmdline);

    g_free(cmdline);
}

static void microvm_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    X86CPU *cpu = X86_CPU(dev);

    cpu->host_phys_bits = true; /* need reliable phys-bits */
    x86_cpu_pre_plug(hotplug_dev, dev, errp);
}

static void microvm_device_plug_cb(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    x86_cpu_plug(hotplug_dev, dev, errp);
}

static void microvm_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                             DeviceState *dev, Error **errp)
{
    error_setg(errp, "unplug not supported by microvm");
}

static void microvm_device_unplug_cb(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    error_setg(errp, "unplug not supported by microvm");
}

static HotplugHandler *microvm_get_hotplug_handler(MachineState *machine,
                                                   DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void microvm_machine_state_init(MachineState *machine)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);

    microvm_memory_init(mms);

    x86_cpus_init(x86ms, CPU_VERSION_LATEST);

    microvm_devices_init(mms);
}

static void microvm_machine_reset(MachineState *machine, ShutdownCause reason)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    CPUState *cs;
    X86CPU *cpu;

    if (!x86_machine_is_acpi_enabled(X86_MACHINE(machine)) &&
        machine->kernel_filename != NULL &&
        mms->auto_kernel_cmdline && !mms->kernel_cmdline_fixed) {
        microvm_fix_kernel_cmdline(machine);
        mms->kernel_cmdline_fixed = true;
    }

    qemu_devices_reset(reason);

    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        x86_cpu_after_reset(cpu);
    }
}

static void microvm_machine_get_rtc(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto rtc = mms->rtc;

    visit_type_OnOffAuto(v, name, &rtc, errp);
}

static void microvm_machine_set_rtc(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->rtc, errp);
}

static void microvm_machine_get_pcie(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto pcie = mms->pcie;

    visit_type_OnOffAuto(v, name, &pcie, errp);
}

static void microvm_machine_set_pcie(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->pcie, errp);
}

static void microvm_machine_get_ioapic2(Object *obj, Visitor *v, const char *name,
                                        void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    OnOffAuto ioapic2 = mms->ioapic2;

    visit_type_OnOffAuto(v, name, &ioapic2, errp);
}

static void microvm_machine_set_ioapic2(Object *obj, Visitor *v, const char *name,
                                        void *opaque, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &mms->ioapic2, errp);
}

static bool microvm_machine_get_isa_serial(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->isa_serial;
}

static void microvm_machine_set_isa_serial(Object *obj, bool value,
                                           Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->isa_serial = value;
}

static bool microvm_machine_get_option_roms(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->option_roms;
}

static void microvm_machine_set_option_roms(Object *obj, bool value,
                                            Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->option_roms = value;
}

static bool microvm_machine_get_auto_kernel_cmdline(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->auto_kernel_cmdline;
}

static void microvm_machine_set_auto_kernel_cmdline(Object *obj, bool value,
                                                    Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->auto_kernel_cmdline = value;
}

static void microvm_machine_done(Notifier *notifier, void *data)
{
    MicrovmMachineState *mms = container_of(notifier, MicrovmMachineState,
                                            machine_done);

    acpi_setup_microvm(mms);
    dt_setup_microvm(mms);
}

static void microvm_powerdown_req(Notifier *notifier, void *data)
{
    MicrovmMachineState *mms = container_of(notifier, MicrovmMachineState,
                                            powerdown_req);
    X86MachineState *x86ms = X86_MACHINE(mms);

    if (x86ms->acpi_dev) {
        Object *obj = OBJECT(x86ms->acpi_dev);
        AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
        adevc->send_event(ACPI_DEVICE_IF(x86ms->acpi_dev),
                          ACPI_POWER_DOWN_STATUS);
    }
}

static void microvm_machine_initfn(Object *obj)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    /* Configuration */
    mms->rtc = ON_OFF_AUTO_AUTO;
    mms->pcie = ON_OFF_AUTO_AUTO;
    mms->ioapic2 = ON_OFF_AUTO_AUTO;
    mms->isa_serial = true;
    mms->option_roms = true;
    mms->auto_kernel_cmdline = true;

    /* State */
    mms->kernel_cmdline_fixed = false;

    mms->machine_done.notify = microvm_machine_done;
    qemu_add_machine_init_done_notifier(&mms->machine_done);
    mms->powerdown_req.notify = microvm_powerdown_req;
    qemu_register_powerdown_notifier(&mms->powerdown_req);
}

GlobalProperty microvm_properties[] = {
    /*
     * pcie host bridge (gpex) on microvm has no io address window,
     * so reserving io space is not going to work.  Turn it off.
     */
    { "pcie-root-port", "io-reserve", "0" },
};

static void microvm_class_init(ObjectClass *oc, void *data)
{
    X86MachineClass *x86mc = X86_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->init = microvm_machine_state_init;

    mc->family = "microvm_i386";
    mc->desc = "microvm (i386)";
    mc->units_per_default_bus = 1;
    mc->no_floppy = 1;
    mc->max_cpus = 288;
    mc->has_hotpluggable_cpus = false;
    mc->auto_enable_numa_with_memhp = false;
    mc->auto_enable_numa_with_memdev = false;
    mc->default_cpu_type = TARGET_DEFAULT_CPU_TYPE;
    mc->nvdimm_supported = false;
    mc->default_ram_id = "microvm.ram";

    /* Avoid relying too much on kernel components */
    mc->default_kernel_irqchip_split = true;

    /* Machine class handlers */
    mc->reset = microvm_machine_reset;

    /* hotplug (for cpu coldplug) */
    mc->get_hotplug_handler = microvm_get_hotplug_handler;
    hc->pre_plug = microvm_device_pre_plug_cb;
    hc->plug = microvm_device_plug_cb;
    hc->unplug_request = microvm_device_unplug_request_cb;
    hc->unplug = microvm_device_unplug_cb;

    x86mc->fwcfg_dma_enabled = true;

    object_class_property_add(oc, MICROVM_MACHINE_RTC, "OnOffAuto",
                              microvm_machine_get_rtc,
                              microvm_machine_set_rtc,
                              NULL, NULL);
    object_class_property_set_description(oc, MICROVM_MACHINE_RTC,
        "Enable MC146818 RTC");

    object_class_property_add(oc, MICROVM_MACHINE_PCIE, "OnOffAuto",
                              microvm_machine_get_pcie,
                              microvm_machine_set_pcie,
                              NULL, NULL);
    object_class_property_set_description(oc, MICROVM_MACHINE_PCIE,
        "Enable PCIe");

    object_class_property_add(oc, MICROVM_MACHINE_IOAPIC2, "OnOffAuto",
                              microvm_machine_get_ioapic2,
                              microvm_machine_set_ioapic2,
                              NULL, NULL);
    object_class_property_set_description(oc, MICROVM_MACHINE_IOAPIC2,
        "Enable second IO-APIC");

    object_class_property_add_bool(oc, MICROVM_MACHINE_ISA_SERIAL,
                                   microvm_machine_get_isa_serial,
                                   microvm_machine_set_isa_serial);
    object_class_property_set_description(oc, MICROVM_MACHINE_ISA_SERIAL,
        "Set off to disable the instantiation an ISA serial port");

    object_class_property_add_bool(oc, MICROVM_MACHINE_OPTION_ROMS,
                                   microvm_machine_get_option_roms,
                                   microvm_machine_set_option_roms);
    object_class_property_set_description(oc, MICROVM_MACHINE_OPTION_ROMS,
        "Set off to disable loading option ROMs");

    object_class_property_add_bool(oc, MICROVM_MACHINE_AUTO_KERNEL_CMDLINE,
                                   microvm_machine_get_auto_kernel_cmdline,
                                   microvm_machine_set_auto_kernel_cmdline);
    object_class_property_set_description(oc,
        MICROVM_MACHINE_AUTO_KERNEL_CMDLINE,
        "Set off to disable adding virtio-mmio devices to the kernel cmdline");

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);

    compat_props_add(mc->compat_props, microvm_properties,
                     G_N_ELEMENTS(microvm_properties));
}

static const TypeInfo microvm_machine_info = {
    .name          = TYPE_MICROVM_MACHINE,
    .parent        = TYPE_X86_MACHINE,
    .instance_size = sizeof(MicrovmMachineState),
    .instance_init = microvm_machine_initfn,
    .class_size    = sizeof(MicrovmMachineClass),
    .class_init    = microvm_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void microvm_machine_init(void)
{
    type_register_static(&microvm_machine_info);
}
type_init(microvm_machine_init);
