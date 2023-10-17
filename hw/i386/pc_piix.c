/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES

#include "qemu/units.h"
#include "hw/char/parallel-isa.h"
#include "hw/dma/i8257.h"
#include "hw/loader.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic.h"
#include "hw/pci-host/i440fx.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/southbridge/piix.h"
#include "hw/display/ramfb.h"
#include "hw/firmware/smbios.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/usb.h"
#include "net/net.h"
#include "hw/ide/isa.h"
#include "hw/ide/pci.h"
#include "hw/irq.h"
#include "sysemu/kvm.h"
#include "hw/i386/kvm/clock.h"
#include "hw/sysbus.h"
#include "hw/i2c/smbus_eeprom.h"
#include "exec/memory.h"
#include "hw/acpi/acpi.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/xen.h"
#ifdef CONFIG_XEN
#include <xen/hvm/hvm_info_table.h>
#include "hw/xen/xen_pt.h"
#endif
#include "hw/xen/xen-x86.h"
#include "hw/xen/xen.h"
#include "migration/global_state.h"
#include "migration/misc.h"
#include "sysemu/numa.h"
#include "hw/hyperv/vmbus-bridge.h"
#include "hw/mem/nvdimm.h"
#include "hw/i386/acpi-build.h"
#include "kvm/kvm-cpu.h"
#include "target/i386/cpu.h"

#define MAX_IDE_BUS 2
#define XEN_IOAPIC_NUM_PIRQS 128ULL

#ifdef CONFIG_IDE_ISA
static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };
#endif

/*
 * Return the global irq number corresponding to a given device irq
 * pin. We could also use the bus number to have a more precise mapping.
 */
static int pc_pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx)
{
    int slot_addend;
    slot_addend = PCI_SLOT(pci_dev->devfn) - 1;
    return (pci_intx + slot_addend) & 3;
}

static void piix_intx_routing_notifier_xen(PCIDevice *dev)
{
    int i;

    /* Scan for updates to PCI link routes (0x60-0x63). */
    for (i = 0; i < PIIX_NUM_PIRQS; i++) {
        uint8_t v = dev->config_read(dev, PIIX_PIRQCA + i, 1);
        if (v & 0x80) {
            v = 0;
        }
        v &= 0xf;
        xen_set_pci_link_route(i, v);
    }
}

/* PC hardware initialisation */
static void pc_init1(MachineState *machine,
                     const char *host_type, const char *pci_type)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    PCIBus *pci_bus = NULL;
    ISABus *isa_bus;
    Object *piix4_pm = NULL;
    qemu_irq smi_irq;
    GSIState *gsi_state;
    BusState *idebus[MAX_IDE_BUS];
    ISADevice *rtc_state;
    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory = NULL;
    MemoryRegion *rom_memory = system_memory;
    ram_addr_t lowmem;
    uint64_t hole64_size = 0;

    /*
     * Calculate ram split, for memory below and above 4G.  It's a bit
     * complicated for backward compatibility reasons ...
     *
     *  - Traditional split is 3.5G (lowmem = 0xe0000000).  This is the
     *    default value for max_ram_below_4g now.
     *
     *  - Then, to gigabyte align the memory, we move the split to 3G
     *    (lowmem = 0xc0000000).  But only in case we have to split in
     *    the first place, i.e. ram_size is larger than (traditional)
     *    lowmem.  And for new machine types (gigabyte_align = true)
     *    only, for live migration compatibility reasons.
     *
     *  - Next the max-ram-below-4g option was added, which allowed to
     *    reduce lowmem to a smaller value, to allow a larger PCI I/O
     *    window below 4G.  qemu doesn't enforce gigabyte alignment here,
     *    but prints a warning.
     *
     *  - Finally max-ram-below-4g got updated to also allow raising lowmem,
     *    so legacy non-PAE guests can get as much memory as possible in
     *    the 32bit address space below 4G.
     *
     *  - Note that Xen has its own ram setup code in xen_ram_init(),
     *    called via xen_hvm_init_pc().
     *
     * Examples:
     *    qemu -M pc-1.7 -m 4G    (old default)    -> 3584M low,  512M high
     *    qemu -M pc -m 4G        (new default)    -> 3072M low, 1024M high
     *    qemu -M pc,max-ram-below-4g=2G -m 4G     -> 2048M low, 2048M high
     *    qemu -M pc,max-ram-below-4g=4G -m 3968M  -> 3968M low (=4G-128M)
     */
    if (xen_enabled()) {
        xen_hvm_init_pc(pcms, &ram_memory);
    } else {
        ram_memory = machine->ram;
        if (!pcms->max_ram_below_4g) {
            pcms->max_ram_below_4g = 0xe0000000; /* default: 3.5G */
        }
        lowmem = pcms->max_ram_below_4g;
        if (machine->ram_size >= pcms->max_ram_below_4g) {
            if (pcmc->gigabyte_align) {
                if (lowmem > 0xc0000000) {
                    lowmem = 0xc0000000;
                }
                if (lowmem & (1 * GiB - 1)) {
                    warn_report("Large machine and max_ram_below_4g "
                                "(%" PRIu64 ") not a multiple of 1G; "
                                "possible bad performance.",
                                pcms->max_ram_below_4g);
                }
            }
        }

        if (machine->ram_size >= lowmem) {
            x86ms->above_4g_mem_size = machine->ram_size - lowmem;
            x86ms->below_4g_mem_size = lowmem;
        } else {
            x86ms->above_4g_mem_size = 0;
            x86ms->below_4g_mem_size = machine->ram_size;
        }
    }

    pc_machine_init_sgx_epc(pcms);
    x86_cpus_init(x86ms, pcmc->default_cpu_version);

    if (kvm_enabled() && pcmc->kvmclock_enabled) {
        kvmclock_create(pcmc->kvmclock_create_always);
    }

    if (pcmc->pci_enabled) {
        Object *phb;

        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
        rom_memory = pci_memory;

        phb = OBJECT(qdev_new(host_type));
        object_property_add_child(OBJECT(machine), "i440fx", phb);
        object_property_set_link(phb, PCI_HOST_PROP_RAM_MEM,
                                 OBJECT(ram_memory), &error_fatal);
        object_property_set_link(phb, PCI_HOST_PROP_PCI_MEM,
                                 OBJECT(pci_memory), &error_fatal);
        object_property_set_link(phb, PCI_HOST_PROP_SYSTEM_MEM,
                                 OBJECT(system_memory), &error_fatal);
        object_property_set_link(phb, PCI_HOST_PROP_IO_MEM,
                                 OBJECT(system_io), &error_fatal);
        object_property_set_uint(phb, PCI_HOST_BELOW_4G_MEM_SIZE,
                                 x86ms->below_4g_mem_size, &error_fatal);
        object_property_set_uint(phb, PCI_HOST_ABOVE_4G_MEM_SIZE,
                                 x86ms->above_4g_mem_size, &error_fatal);
        object_property_set_str(phb, I440FX_HOST_PROP_PCI_TYPE, pci_type,
                                &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(phb), &error_fatal);

        pci_bus = PCI_BUS(qdev_get_child_bus(DEVICE(phb), "pci.0"));
        pci_bus_map_irqs(pci_bus,
                         xen_enabled() ? xen_pci_slot_get_pirq
                                       : pc_pci_slot_get_pirq);
        pcms->bus = pci_bus;

        hole64_size = object_property_get_uint(phb,
                                               PCI_HOST_PROP_PCI_HOLE64_SIZE,
                                               &error_abort);
    }

    pc_guest_info_init(pcms);

    if (pcmc->smbios_defaults) {
        MachineClass *mc = MACHINE_GET_CLASS(machine);
        /* These values are guest ABI, do not change */
        smbios_set_defaults("QEMU", mc->desc,
                            mc->name, pcmc->smbios_legacy_mode,
                            pcmc->smbios_uuid_encoded,
                            pcms->smbios_entry_point_type);
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(pcms, system_memory, rom_memory, hole64_size);
    } else {
        assert(machine->ram_size == x86ms->below_4g_mem_size +
                                    x86ms->above_4g_mem_size);

        pc_system_flash_cleanup_unused(pcms);
        if (machine->kernel_filename != NULL) {
            /* For xen HVM direct kernel boot, load linux here */
            xen_load_linux(pcms);
        }
    }

    gsi_state = pc_gsi_create(&x86ms->gsi, pcmc->pci_enabled);

    if (pcmc->pci_enabled) {
        PCIDevice *pci_dev;
        DeviceState *dev;
        size_t i;

        pci_dev = pci_new_multifunction(-1, pcms->south_bridge);
        object_property_set_bool(OBJECT(pci_dev), "has-usb",
                                 machine_usb(machine), &error_abort);
        object_property_set_bool(OBJECT(pci_dev), "has-acpi",
                                 x86_machine_is_acpi_enabled(x86ms),
                                 &error_abort);
        object_property_set_bool(OBJECT(pci_dev), "has-pic", false,
                                 &error_abort);
        object_property_set_bool(OBJECT(pci_dev), "has-pit", false,
                                 &error_abort);
        qdev_prop_set_uint32(DEVICE(pci_dev), "smb_io_base", 0xb100);
        object_property_set_bool(OBJECT(pci_dev), "smm-enabled",
                                 x86_machine_is_smm_enabled(x86ms),
                                 &error_abort);
        dev = DEVICE(pci_dev);
        for (i = 0; i < ISA_NUM_IRQS; i++) {
            qdev_connect_gpio_out_named(dev, "isa-irqs", i, x86ms->gsi[i]);
        }
        pci_realize_and_unref(pci_dev, pci_bus, &error_fatal);

        if (xen_enabled()) {
            pci_device_set_intx_routing_notifier(
                        pci_dev, piix_intx_routing_notifier_xen);

            /*
             * Xen supports additional interrupt routes from the PCI devices to
             * the IOAPIC: the four pins of each PCI device on the bus are also
             * connected to the IOAPIC directly.
             * These additional routes can be discovered through ACPI.
             */
            pci_bus_irqs(pci_bus, xen_intx_set_irq, pci_dev,
                         XEN_IOAPIC_NUM_PIRQS);
        }

        isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(pci_dev), "isa.0"));
        rtc_state = ISA_DEVICE(object_resolve_path_component(OBJECT(pci_dev),
                                                             "rtc"));
        piix4_pm = object_resolve_path_component(OBJECT(pci_dev), "pm");
        dev = DEVICE(object_resolve_path_component(OBJECT(pci_dev), "ide"));
        pci_ide_create_devs(PCI_DEVICE(dev));
        idebus[0] = qdev_get_child_bus(dev, "ide.0");
        idebus[1] = qdev_get_child_bus(dev, "ide.1");
    } else {
        isa_bus = isa_bus_new(NULL, system_memory, system_io,
                              &error_abort);
        isa_bus_register_input_irqs(isa_bus, x86ms->gsi);

        rtc_state = isa_new(TYPE_MC146818_RTC);
        qdev_prop_set_int32(DEVICE(rtc_state), "base_year", 2000);
        isa_realize_and_unref(rtc_state, isa_bus, &error_fatal);

        i8257_dma_init(isa_bus, 0);
        pcms->hpet_enabled = false;
        idebus[0] = NULL;
        idebus[1] = NULL;
    }

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        pc_i8259_create(isa_bus, gsi_state->i8259_irq);
    }

    if (pcmc->pci_enabled) {
        ioapic_init_gsi(gsi_state, "i440fx");
    }

    if (tcg_enabled()) {
        x86_register_ferr_irq(x86ms->gsi[13]);
    }

    pc_vga_init(isa_bus, pcmc->pci_enabled ? pci_bus : NULL);

    assert(pcms->vmport != ON_OFF_AUTO__MAX);
    if (pcms->vmport == ON_OFF_AUTO_AUTO) {
        pcms->vmport = xen_enabled() ? ON_OFF_AUTO_OFF : ON_OFF_AUTO_ON;
    }

    /* init basic PC hardware */
    pc_basic_device_init(pcms, isa_bus, x86ms->gsi, rtc_state, true,
                         0x4);

    pc_nic_init(pcmc, isa_bus, pci_bus, pcms->xenbus);

    if (pcmc->pci_enabled) {
        pc_cmos_init(pcms, idebus[0], idebus[1], rtc_state);
    }
#ifdef CONFIG_IDE_ISA
    else {
        DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
        int i;

        ide_drive_get(hd, ARRAY_SIZE(hd));
        for (i = 0; i < MAX_IDE_BUS; i++) {
            ISADevice *dev;
            char busname[] = "ide.0";
            dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
                               ide_irq[i],
                               hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
            /*
             * The ide bus name is ide.0 for the first bus and ide.1 for the
             * second one.
             */
            busname[4] = '0' + i;
            idebus[i] = qdev_get_child_bus(DEVICE(dev), busname);
        }
        pc_cmos_init(pcms, idebus[0], idebus[1], rtc_state);
    }
#endif

    if (piix4_pm) {
        smi_irq = qemu_allocate_irq(pc_acpi_smi_interrupt, first_cpu, 0);

        qdev_connect_gpio_out_named(DEVICE(piix4_pm), "smi-irq", 0, smi_irq);
        pcms->smbus = I2C_BUS(qdev_get_child_bus(DEVICE(piix4_pm), "i2c"));
        /* TODO: Populate SPD eeprom data.  */
        smbus_eeprom_init(pcms->smbus, 8, NULL, 0);

        object_property_add_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                                 TYPE_HOTPLUG_HANDLER,
                                 (Object **)&x86ms->acpi_dev,
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_STRONG);
        object_property_set_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                                 piix4_pm, &error_abort);
    }

    if (machine->nvdimms_state->is_enabled) {
        nvdimm_init_acpi_state(machine->nvdimms_state, system_io,
                               x86_nvdimm_acpi_dsmio,
                               x86ms->fw_cfg, OBJECT(pcms));
    }
}

typedef enum PCSouthBridgeOption {
    PC_SOUTH_BRIDGE_OPTION_PIIX3,
    PC_SOUTH_BRIDGE_OPTION_PIIX4,
    PC_SOUTH_BRIDGE_OPTION_MAX,
} PCSouthBridgeOption;

static const QEnumLookup PCSouthBridgeOption_lookup = {
    .array = (const char *const[]) {
        [PC_SOUTH_BRIDGE_OPTION_PIIX3] = TYPE_PIIX3_DEVICE,
        [PC_SOUTH_BRIDGE_OPTION_PIIX4] = TYPE_PIIX4_PCI_DEVICE,
    },
    .size = PC_SOUTH_BRIDGE_OPTION_MAX
};

#define NotifyVmexitOption_str(val) \
    qapi_enum_lookup(&NotifyVmexitOption_lookup, (val))

static int pc_get_south_bridge(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    int i;

    for (i = 0; i < PCSouthBridgeOption_lookup.size; i++) {
        if (g_strcmp0(PCSouthBridgeOption_lookup.array[i],
                      pcms->south_bridge) == 0) {
            return i;
        }
    }

    error_setg(errp, "Invalid south bridge value set");
    return 0;
}

static void pc_set_south_bridge(Object *obj, int value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    if (value < 0) {
        error_setg(errp, "Value can't be negative");
        return;
    }

    if (value >= PCSouthBridgeOption_lookup.size) {
        error_setg(errp, "Value too big");
        return;
    }

    pcms->south_bridge = PCSouthBridgeOption_lookup.array[value];
}

/* Looking for a pc_compat_2_4() function? It doesn't exist.
 * pc_compat_*() functions that run on machine-init time and
 * change global QEMU state are deprecated. Please don't create
 * one, and implement any pc-*-2.4 (and newer) compat code in
 * hw_compat_*, pc_compat_*, or * pc_*_machine_options().
 */

static void pc_compat_2_3_fn(MachineState *machine)
{
    X86MachineState *x86ms = X86_MACHINE(machine);
    if (kvm_enabled()) {
        x86ms->smm = ON_OFF_AUTO_OFF;
    }
}

static void pc_compat_2_2_fn(MachineState *machine)
{
    pc_compat_2_3_fn(machine);
}

static void pc_compat_2_1_fn(MachineState *machine)
{
    pc_compat_2_2_fn(machine);
    x86_cpu_change_kvm_default("svm", NULL);
}

static void pc_compat_2_0_fn(MachineState *machine)
{
    pc_compat_2_1_fn(machine);
}

#ifdef CONFIG_ISAPC
static void pc_init_isa(MachineState *machine)
{
    pc_init1(machine, TYPE_I440FX_PCI_HOST_BRIDGE, TYPE_I440FX_PCI_DEVICE);
}
#endif

#ifdef CONFIG_XEN
static void pc_xen_hvm_init_pci(MachineState *machine)
{
    const char *pci_type = xen_igd_gfx_pt_enabled() ?
                TYPE_IGD_PASSTHROUGH_I440FX_PCI_DEVICE : TYPE_I440FX_PCI_DEVICE;

    pc_init1(machine,
             TYPE_I440FX_PCI_HOST_BRIDGE,
             pci_type);
}

static void pc_xen_hvm_init(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);

    if (!xen_enabled()) {
        error_report("xenfv machine requires the xen accelerator");
        exit(1);
    }

    pc_xen_hvm_init_pci(machine);
    xen_igd_reserve_slot(pcms->bus);
    pci_create_simple(pcms->bus, -1, "xen-platform");
}
#endif

#define DEFINE_I440FX_MACHINE(suffix, name, compatfn, optionfn) \
    static void pc_init_##suffix(MachineState *machine) \
    { \
        void (*compat)(MachineState *m) = (compatfn); \
        if (compat) { \
            compat(machine); \
        } \
        pc_init1(machine, TYPE_I440FX_PCI_HOST_BRIDGE, \
                 TYPE_I440FX_PCI_DEVICE); \
    } \
    DEFINE_PC_MACHINE(suffix, name, pc_init_##suffix, optionfn)

static void pc_i440fx_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    ObjectClass *oc = OBJECT_CLASS(m);
    pcmc->default_south_bridge = TYPE_PIIX3_DEVICE;
    pcmc->pci_root_uid = 0;
    pcmc->default_cpu_version = 1;

    m->family = "pc_piix";
    m->desc = "Standard PC (i440FX + PIIX, 1996)";
    m->default_machine_opts = "firmware=bios-256k.bin";
    m->default_display = "std";
    m->default_nic = "e1000";
    m->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_RAMFB_DEVICE);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_VMBUS_BRIDGE);

    object_class_property_add_enum(oc, "x-south-bridge", "PCSouthBridgeOption",
                                   &PCSouthBridgeOption_lookup,
                                   pc_get_south_bridge,
                                   pc_set_south_bridge);
    object_class_property_set_description(oc, "x-south-bridge",
                                     "Use a different south bridge than PIIX3");
}

static void pc_i440fx_8_2_machine_options(MachineClass *m)
{
    pc_i440fx_machine_options(m);
    m->alias = "pc";
    m->is_default = true;
}

DEFINE_I440FX_MACHINE(v8_2, "pc-i440fx-8.2", NULL,
                      pc_i440fx_8_2_machine_options);

static void pc_i440fx_8_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_8_2_machine_options(m);
    m->alias = NULL;
    m->is_default = false;
    pcmc->broken_32bit_mem_addr_check = true;

    compat_props_add(m->compat_props, hw_compat_8_1, hw_compat_8_1_len);
    compat_props_add(m->compat_props, pc_compat_8_1, pc_compat_8_1_len);
}

DEFINE_I440FX_MACHINE(v8_1, "pc-i440fx-8.1", NULL,
                      pc_i440fx_8_1_machine_options);

static void pc_i440fx_8_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_8_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_8_0, hw_compat_8_0_len);
    compat_props_add(m->compat_props, pc_compat_8_0, pc_compat_8_0_len);

    /* For pc-i44fx-8.0 and older, use SMBIOS 2.8 by default */
    pcmc->default_smbios_ep_type = SMBIOS_ENTRY_POINT_TYPE_32;
}

DEFINE_I440FX_MACHINE(v8_0, "pc-i440fx-8.0", NULL,
                      pc_i440fx_8_0_machine_options);

static void pc_i440fx_7_2_machine_options(MachineClass *m)
{
    pc_i440fx_8_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_7_2, hw_compat_7_2_len);
    compat_props_add(m->compat_props, pc_compat_7_2, pc_compat_7_2_len);
}

DEFINE_I440FX_MACHINE(v7_2, "pc-i440fx-7.2", NULL,
                      pc_i440fx_7_2_machine_options);

static void pc_i440fx_7_1_machine_options(MachineClass *m)
{
    pc_i440fx_7_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_7_1, hw_compat_7_1_len);
    compat_props_add(m->compat_props, pc_compat_7_1, pc_compat_7_1_len);
}

DEFINE_I440FX_MACHINE(v7_1, "pc-i440fx-7.1", NULL,
                      pc_i440fx_7_1_machine_options);

static void pc_i440fx_7_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pc_i440fx_7_1_machine_options(m);
    pcmc->enforce_amd_1tb_hole = false;
    compat_props_add(m->compat_props, hw_compat_7_0, hw_compat_7_0_len);
    compat_props_add(m->compat_props, pc_compat_7_0, pc_compat_7_0_len);
}

DEFINE_I440FX_MACHINE(v7_0, "pc-i440fx-7.0", NULL,
                      pc_i440fx_7_0_machine_options);

static void pc_i440fx_6_2_machine_options(MachineClass *m)
{
    pc_i440fx_7_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_2, hw_compat_6_2_len);
    compat_props_add(m->compat_props, pc_compat_6_2, pc_compat_6_2_len);
}

DEFINE_I440FX_MACHINE(v6_2, "pc-i440fx-6.2", NULL,
                      pc_i440fx_6_2_machine_options);

static void pc_i440fx_6_1_machine_options(MachineClass *m)
{
    pc_i440fx_6_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_1, hw_compat_6_1_len);
    compat_props_add(m->compat_props, pc_compat_6_1, pc_compat_6_1_len);
    m->smp_props.prefer_sockets = true;
}

DEFINE_I440FX_MACHINE(v6_1, "pc-i440fx-6.1", NULL,
                      pc_i440fx_6_1_machine_options);

static void pc_i440fx_6_0_machine_options(MachineClass *m)
{
    pc_i440fx_6_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_0, hw_compat_6_0_len);
    compat_props_add(m->compat_props, pc_compat_6_0, pc_compat_6_0_len);
}

DEFINE_I440FX_MACHINE(v6_0, "pc-i440fx-6.0", NULL,
                      pc_i440fx_6_0_machine_options);

static void pc_i440fx_5_2_machine_options(MachineClass *m)
{
    pc_i440fx_6_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_5_2, hw_compat_5_2_len);
    compat_props_add(m->compat_props, pc_compat_5_2, pc_compat_5_2_len);
}

DEFINE_I440FX_MACHINE(v5_2, "pc-i440fx-5.2", NULL,
                      pc_i440fx_5_2_machine_options);

static void pc_i440fx_5_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_5_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_5_1, hw_compat_5_1_len);
    compat_props_add(m->compat_props, pc_compat_5_1, pc_compat_5_1_len);
    pcmc->kvmclock_create_always = false;
    pcmc->pci_root_uid = 1;
}

DEFINE_I440FX_MACHINE(v5_1, "pc-i440fx-5.1", NULL,
                      pc_i440fx_5_1_machine_options);

static void pc_i440fx_5_0_machine_options(MachineClass *m)
{
    pc_i440fx_5_1_machine_options(m);
    m->numa_mem_supported = true;
    compat_props_add(m->compat_props, hw_compat_5_0, hw_compat_5_0_len);
    compat_props_add(m->compat_props, pc_compat_5_0, pc_compat_5_0_len);
    m->auto_enable_numa_with_memdev = false;
}

DEFINE_I440FX_MACHINE(v5_0, "pc-i440fx-5.0", NULL,
                      pc_i440fx_5_0_machine_options);

static void pc_i440fx_4_2_machine_options(MachineClass *m)
{
    pc_i440fx_5_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_4_2, hw_compat_4_2_len);
    compat_props_add(m->compat_props, pc_compat_4_2, pc_compat_4_2_len);
}

DEFINE_I440FX_MACHINE(v4_2, "pc-i440fx-4.2", NULL,
                      pc_i440fx_4_2_machine_options);

static void pc_i440fx_4_1_machine_options(MachineClass *m)
{
    pc_i440fx_4_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_4_1, hw_compat_4_1_len);
    compat_props_add(m->compat_props, pc_compat_4_1, pc_compat_4_1_len);
}

DEFINE_I440FX_MACHINE(v4_1, "pc-i440fx-4.1", NULL,
                      pc_i440fx_4_1_machine_options);

static void pc_i440fx_4_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pc_i440fx_4_1_machine_options(m);
    pcmc->default_cpu_version = CPU_VERSION_LEGACY;
    compat_props_add(m->compat_props, hw_compat_4_0, hw_compat_4_0_len);
    compat_props_add(m->compat_props, pc_compat_4_0, pc_compat_4_0_len);
}

DEFINE_I440FX_MACHINE(v4_0, "pc-i440fx-4.0", NULL,
                      pc_i440fx_4_0_machine_options);

static void pc_i440fx_3_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_4_0_machine_options(m);
    m->smbus_no_migration_support = true;
    pcmc->pvh_enabled = false;
    compat_props_add(m->compat_props, hw_compat_3_1, hw_compat_3_1_len);
    compat_props_add(m->compat_props, pc_compat_3_1, pc_compat_3_1_len);
}

DEFINE_I440FX_MACHINE(v3_1, "pc-i440fx-3.1", NULL,
                      pc_i440fx_3_1_machine_options);

static void pc_i440fx_3_0_machine_options(MachineClass *m)
{
    pc_i440fx_3_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_3_0, hw_compat_3_0_len);
    compat_props_add(m->compat_props, pc_compat_3_0, pc_compat_3_0_len);
}

DEFINE_I440FX_MACHINE(v3_0, "pc-i440fx-3.0", NULL,
                      pc_i440fx_3_0_machine_options);

static void pc_i440fx_2_12_machine_options(MachineClass *m)
{
    pc_i440fx_3_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_12, hw_compat_2_12_len);
    compat_props_add(m->compat_props, pc_compat_2_12, pc_compat_2_12_len);
}

DEFINE_I440FX_MACHINE(v2_12, "pc-i440fx-2.12", NULL,
                      pc_i440fx_2_12_machine_options);

static void pc_i440fx_2_11_machine_options(MachineClass *m)
{
    pc_i440fx_2_12_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_11, hw_compat_2_11_len);
    compat_props_add(m->compat_props, pc_compat_2_11, pc_compat_2_11_len);
}

DEFINE_I440FX_MACHINE(v2_11, "pc-i440fx-2.11", NULL,
                      pc_i440fx_2_11_machine_options);

static void pc_i440fx_2_10_machine_options(MachineClass *m)
{
    pc_i440fx_2_11_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_10, hw_compat_2_10_len);
    compat_props_add(m->compat_props, pc_compat_2_10, pc_compat_2_10_len);
    m->auto_enable_numa_with_memhp = false;
}

DEFINE_I440FX_MACHINE(v2_10, "pc-i440fx-2.10", NULL,
                      pc_i440fx_2_10_machine_options);

static void pc_i440fx_2_9_machine_options(MachineClass *m)
{
    pc_i440fx_2_10_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_9, hw_compat_2_9_len);
    compat_props_add(m->compat_props, pc_compat_2_9, pc_compat_2_9_len);
}

DEFINE_I440FX_MACHINE(v2_9, "pc-i440fx-2.9", NULL,
                      pc_i440fx_2_9_machine_options);

static void pc_i440fx_2_8_machine_options(MachineClass *m)
{
    pc_i440fx_2_9_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_8, hw_compat_2_8_len);
    compat_props_add(m->compat_props, pc_compat_2_8, pc_compat_2_8_len);
}

DEFINE_I440FX_MACHINE(v2_8, "pc-i440fx-2.8", NULL,
                      pc_i440fx_2_8_machine_options);

static void pc_i440fx_2_7_machine_options(MachineClass *m)
{
    pc_i440fx_2_8_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_7, hw_compat_2_7_len);
    compat_props_add(m->compat_props, pc_compat_2_7, pc_compat_2_7_len);
}

DEFINE_I440FX_MACHINE(v2_7, "pc-i440fx-2.7", NULL,
                      pc_i440fx_2_7_machine_options);

static void pc_i440fx_2_6_machine_options(MachineClass *m)
{
    X86MachineClass *x86mc = X86_MACHINE_CLASS(m);
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_2_7_machine_options(m);
    pcmc->legacy_cpu_hotplug = true;
    x86mc->fwcfg_dma_enabled = false;
    compat_props_add(m->compat_props, hw_compat_2_6, hw_compat_2_6_len);
    compat_props_add(m->compat_props, pc_compat_2_6, pc_compat_2_6_len);
}

DEFINE_I440FX_MACHINE(v2_6, "pc-i440fx-2.6", NULL,
                      pc_i440fx_2_6_machine_options);

static void pc_i440fx_2_5_machine_options(MachineClass *m)
{
    X86MachineClass *x86mc = X86_MACHINE_CLASS(m);

    pc_i440fx_2_6_machine_options(m);
    x86mc->save_tsc_khz = false;
    m->legacy_fw_cfg_order = 1;
    compat_props_add(m->compat_props, hw_compat_2_5, hw_compat_2_5_len);
    compat_props_add(m->compat_props, pc_compat_2_5, pc_compat_2_5_len);
}

DEFINE_I440FX_MACHINE(v2_5, "pc-i440fx-2.5", NULL,
                      pc_i440fx_2_5_machine_options);

static void pc_i440fx_2_4_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_2_5_machine_options(m);
    m->hw_version = "2.4.0";
    pcmc->broken_reserved_end = true;
    compat_props_add(m->compat_props, hw_compat_2_4, hw_compat_2_4_len);
    compat_props_add(m->compat_props, pc_compat_2_4, pc_compat_2_4_len);
}

DEFINE_I440FX_MACHINE(v2_4, "pc-i440fx-2.4", NULL,
                      pc_i440fx_2_4_machine_options)

static void pc_i440fx_2_3_machine_options(MachineClass *m)
{
    pc_i440fx_2_4_machine_options(m);
    m->hw_version = "2.3.0";
    m->deprecation_reason = "old and unattended - use a newer version instead";
    compat_props_add(m->compat_props, hw_compat_2_3, hw_compat_2_3_len);
    compat_props_add(m->compat_props, pc_compat_2_3, pc_compat_2_3_len);
}

DEFINE_I440FX_MACHINE(v2_3, "pc-i440fx-2.3", pc_compat_2_3_fn,
                      pc_i440fx_2_3_machine_options);

static void pc_i440fx_2_2_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_2_3_machine_options(m);
    m->hw_version = "2.2.0";
    m->default_machine_opts = "firmware=bios-256k.bin,suppress-vmdesc=on";
    compat_props_add(m->compat_props, hw_compat_2_2, hw_compat_2_2_len);
    compat_props_add(m->compat_props, pc_compat_2_2, pc_compat_2_2_len);
    pcmc->rsdp_in_ram = false;
    pcmc->resizable_acpi_blob = false;
}

DEFINE_I440FX_MACHINE(v2_2, "pc-i440fx-2.2", pc_compat_2_2_fn,
                      pc_i440fx_2_2_machine_options);

static void pc_i440fx_2_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_2_2_machine_options(m);
    m->hw_version = "2.1.0";
    m->default_display = NULL;
    compat_props_add(m->compat_props, hw_compat_2_1, hw_compat_2_1_len);
    compat_props_add(m->compat_props, pc_compat_2_1, pc_compat_2_1_len);
    pcmc->smbios_uuid_encoded = false;
    pcmc->enforce_aligned_dimm = false;
}

DEFINE_I440FX_MACHINE(v2_1, "pc-i440fx-2.1", pc_compat_2_1_fn,
                      pc_i440fx_2_1_machine_options);

static void pc_i440fx_2_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_i440fx_2_1_machine_options(m);
    m->hw_version = "2.0.0";
    compat_props_add(m->compat_props, pc_compat_2_0, pc_compat_2_0_len);
    pcmc->smbios_legacy_mode = true;
    pcmc->has_reserved_memory = false;
    /* This value depends on the actual DSDT and SSDT compiled into
     * the source QEMU; unfortunately it depends on the binary and
     * not on the machine type, so we cannot make pc-i440fx-1.7 work on
     * both QEMU 1.7 and QEMU 2.0.
     *
     * Large variations cause migration to fail for more than one
     * consecutive value of the "-smp" maxcpus option.
     *
     * For small variations of the kind caused by different iasl versions,
     * the 4k rounding usually leaves slack.  However, there could be still
     * one or two values that break.  For QEMU 1.7 and QEMU 2.0 the
     * slack is only ~10 bytes before one "-smp maxcpus" value breaks!
     *
     * 6652 is valid for QEMU 2.0, the right value for pc-i440fx-1.7 on
     * QEMU 1.7 it is 6414.  For RHEL/CentOS 7.0 it is 6418.
     */
    pcmc->legacy_acpi_table_size = 6652;
    pcmc->acpi_data_size = 0x10000;
}

DEFINE_I440FX_MACHINE(v2_0, "pc-i440fx-2.0", pc_compat_2_0_fn,
                      pc_i440fx_2_0_machine_options);

#ifdef CONFIG_ISAPC
static void isapc_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    m->desc = "ISA-only PC";
    m->max_cpus = 1;
    m->option_rom_has_mr = true;
    m->rom_file_has_mr = false;
    pcmc->pci_enabled = false;
    pcmc->has_acpi_build = false;
    pcmc->smbios_defaults = false;
    pcmc->gigabyte_align = false;
    pcmc->smbios_legacy_mode = true;
    pcmc->has_reserved_memory = false;
    m->default_nic = "ne2k_isa";
    m->default_cpu_type = X86_CPU_TYPE_NAME("486");
    m->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
}

DEFINE_PC_MACHINE(isapc, "isapc", pc_init_isa,
                  isapc_machine_options);
#endif

#ifdef CONFIG_XEN
static void xenfv_4_2_machine_options(MachineClass *m)
{
    pc_i440fx_4_2_machine_options(m);
    m->desc = "Xen Fully-virtualized PC";
    m->max_cpus = HVM_MAX_VCPUS;
    m->default_machine_opts = "accel=xen,suppress-vmdesc=on";
}

DEFINE_PC_MACHINE(xenfv_4_2, "xenfv-4.2", pc_xen_hvm_init,
                  xenfv_4_2_machine_options);

static void xenfv_3_1_machine_options(MachineClass *m)
{
    pc_i440fx_3_1_machine_options(m);
    m->desc = "Xen Fully-virtualized PC";
    m->alias = "xenfv";
    m->max_cpus = HVM_MAX_VCPUS;
    m->default_machine_opts = "accel=xen,suppress-vmdesc=on";
}

DEFINE_PC_MACHINE(xenfv, "xenfv-3.1", pc_xen_hvm_init,
                  xenfv_3_1_machine_options);
#endif
