/*
 * Q35 chipset based pc system emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2009, 2010
 *               Isaku Yamahata <yamahata at valinux co jp>
 *               VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This is based on pc.c, but heavily modified.
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
#include "qemu/units.h"
#include "hw/acpi/acpi.h"
#include "hw/char/parallel-isa.h"
#include "hw/loader.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/rtc/mc146818rtc.h"
#include "sysemu/tcg.h"
#include "sysemu/kvm.h"
#include "hw/i386/kvm/clock.h"
#include "hw/pci-host/q35.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/i386/amd_iommu.h"
#include "hw/i386/intel_iommu.h"
#include "hw/display/ramfb.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci-pci.h"
#include "hw/intc/ioapic.h"
#include "hw/southbridge/ich9.h"
#include "hw/usb.h"
#include "hw/usb/hcd-uhci.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/numa.h"
#include "hw/hyperv/vmbus-bridge.h"
#include "hw/mem/nvdimm.h"
#include "hw/i386/acpi-build.h"
#include "target/i386/cpu.h"

/* ICH9 AHCI has 6 ports */
#define MAX_SATA_PORTS     6

struct ehci_companions {
    const char *name;
    int func;
    int port;
};

static const struct ehci_companions ich9_1d[] = {
    { .name = TYPE_ICH9_USB_UHCI(1), .func = 0, .port = 0 },
    { .name = TYPE_ICH9_USB_UHCI(2), .func = 1, .port = 2 },
    { .name = TYPE_ICH9_USB_UHCI(3), .func = 2, .port = 4 },
};

static const struct ehci_companions ich9_1a[] = {
    { .name = TYPE_ICH9_USB_UHCI(4), .func = 0, .port = 0 },
    { .name = TYPE_ICH9_USB_UHCI(5), .func = 1, .port = 2 },
    { .name = TYPE_ICH9_USB_UHCI(6), .func = 2, .port = 4 },
};

static int ehci_create_ich9_with_companions(PCIBus *bus, int slot)
{
    const struct ehci_companions *comp;
    PCIDevice *ehci, *uhci;
    BusState *usbbus;
    const char *name;
    int i;

    switch (slot) {
    case 0x1d:
        name = "ich9-usb-ehci1";
        comp = ich9_1d;
        break;
    case 0x1a:
        name = "ich9-usb-ehci2";
        comp = ich9_1a;
        break;
    default:
        return -1;
    }

    ehci = pci_new_multifunction(PCI_DEVFN(slot, 7), name);
    pci_realize_and_unref(ehci, bus, &error_fatal);
    usbbus = QLIST_FIRST(&ehci->qdev.child_bus);

    for (i = 0; i < 3; i++) {
        uhci = pci_new_multifunction(PCI_DEVFN(slot, comp[i].func),
                                     comp[i].name);
        qdev_prop_set_string(&uhci->qdev, "masterbus", usbbus->name);
        qdev_prop_set_uint32(&uhci->qdev, "firstport", comp[i].port);
        pci_realize_and_unref(uhci, bus, &error_fatal);
    }
    return 0;
}

/* PC hardware initialisation */
static void pc_q35_init(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(machine);
    Object *phb;
    PCIDevice *lpc;
    DeviceState *lpc_dev;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    MemoryRegion *pci_memory = g_new(MemoryRegion, 1);
    GSIState *gsi_state;
    ISABus *isa_bus;
    int i;
    ram_addr_t lowmem;
    DriveInfo *hd[MAX_SATA_PORTS];
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    bool acpi_pcihp;
    bool keep_pci_slot_hpc;
    uint64_t pci_hole64_size = 0;

    assert(pcmc->pci_enabled);

    /* Check whether RAM fits below 4G (leaving 1/2 GByte for IO memory
     * and 256 Mbytes for PCI Express Enhanced Configuration Access Mapping
     * also known as MMCFG).
     * If it doesn't, we need to split it in chunks below and above 4G.
     * In any case, try to make sure that guest addresses aligned at
     * 1G boundaries get mapped to host addresses aligned at 1G boundaries.
     */
    if (machine->ram_size >= 0xb0000000) {
        lowmem = 0x80000000;
    } else {
        lowmem = 0xb0000000;
    }

    /* Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(qemu limit, user limit).
     */
    if (!pcms->max_ram_below_4g) {
        pcms->max_ram_below_4g = 4 * GiB;
    }
    if (lowmem > pcms->max_ram_below_4g) {
        lowmem = pcms->max_ram_below_4g;
        if (machine->ram_size - lowmem > lowmem &&
            lowmem & (1 * GiB - 1)) {
            warn_report("There is possibly poor performance as the ram size "
                        " (0x%" PRIx64 ") is more then twice the size of"
                        " max-ram-below-4g (%"PRIu64") and"
                        " max-ram-below-4g is not a multiple of 1G.",
                        (uint64_t)machine->ram_size, pcms->max_ram_below_4g);
        }
    }

    if (machine->ram_size >= lowmem) {
        x86ms->above_4g_mem_size = machine->ram_size - lowmem;
        x86ms->below_4g_mem_size = lowmem;
    } else {
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = machine->ram_size;
    }

    pc_machine_init_sgx_epc(pcms);
    x86_cpus_init(x86ms, pcmc->default_cpu_version);

    if (kvm_enabled()) {
        kvmclock_create(pcmc->kvmclock_create_always);
    }

    /* create pci host bus */
    phb = OBJECT(qdev_new(TYPE_Q35_HOST_DEVICE));

    pci_hole64_size = object_property_get_uint(phb,
                                               PCI_HOST_PROP_PCI_HOLE64_SIZE,
                                               &error_abort);

    /* allocate ram and load rom/bios */
    memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
    pc_memory_init(pcms, system_memory, pci_memory, pci_hole64_size);

    object_property_add_child(OBJECT(machine), "q35", phb);
    object_property_set_link(phb, PCI_HOST_PROP_RAM_MEM,
                             OBJECT(machine->ram), NULL);
    object_property_set_link(phb, PCI_HOST_PROP_PCI_MEM,
                             OBJECT(pci_memory), NULL);
    object_property_set_link(phb, PCI_HOST_PROP_SYSTEM_MEM,
                             OBJECT(system_memory), NULL);
    object_property_set_link(phb, PCI_HOST_PROP_IO_MEM,
                             OBJECT(system_io), NULL);
    object_property_set_int(phb, PCI_HOST_BELOW_4G_MEM_SIZE,
                            x86ms->below_4g_mem_size, NULL);
    object_property_set_int(phb, PCI_HOST_ABOVE_4G_MEM_SIZE,
                            x86ms->above_4g_mem_size, NULL);
    object_property_set_bool(phb, PCI_HOST_BYPASS_IOMMU,
                             pcms->default_bus_bypass_iommu, NULL);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(phb), &error_fatal);

    /* pci */
    pcms->pcibus = PCI_BUS(qdev_get_child_bus(DEVICE(phb), "pcie.0"));

    /* irq lines */
    gsi_state = pc_gsi_create(&x86ms->gsi, true);

    /* create ISA bus */
    lpc = pci_new_multifunction(PCI_DEVFN(ICH9_LPC_DEV, ICH9_LPC_FUNC),
                                TYPE_ICH9_LPC_DEVICE);
    lpc_dev = DEVICE(lpc);
    qdev_prop_set_bit(lpc_dev, "smm-enabled",
                      x86_machine_is_smm_enabled(x86ms));
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        qdev_connect_gpio_out_named(lpc_dev, ICH9_GPIO_GSI, i, x86ms->gsi[i]);
    }
    pci_realize_and_unref(lpc, pcms->pcibus, &error_fatal);

    x86ms->rtc = ISA_DEVICE(object_resolve_path_component(OBJECT(lpc), "rtc"));

    object_property_add_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                             TYPE_HOTPLUG_HANDLER,
                             (Object **)&x86ms->acpi_dev,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_set_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                             OBJECT(lpc), &error_abort);

    acpi_pcihp = object_property_get_bool(OBJECT(lpc),
                                          ACPI_PM_PROP_ACPI_PCIHP_BRIDGE,
                                          NULL);

    keep_pci_slot_hpc = object_property_get_bool(OBJECT(lpc),
                                                 "x-keep-pci-slot-hpc",
                                                 NULL);

    if (!keep_pci_slot_hpc && acpi_pcihp) {
        object_register_sugar_prop(TYPE_PCIE_SLOT,
                                   "x-do-not-expose-native-hotplug-cap",
                                   "true", true);
    }

    isa_bus = ISA_BUS(qdev_get_child_bus(lpc_dev, "isa.0"));

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        pc_i8259_create(isa_bus, gsi_state->i8259_irq);
    }

    ioapic_init_gsi(gsi_state, OBJECT(phb));

    if (tcg_enabled()) {
        x86_register_ferr_irq(x86ms->gsi[13]);
    }

    assert(pcms->vmport != ON_OFF_AUTO__MAX);
    if (pcms->vmport == ON_OFF_AUTO_AUTO) {
        pcms->vmport = ON_OFF_AUTO_ON;
    }

    /* init basic PC hardware */
    pc_basic_device_init(pcms, isa_bus, x86ms->gsi, x86ms->rtc, !mc->no_floppy,
                         0xff0104);

    if (pcms->sata_enabled) {
        PCIDevice *pdev;
        AHCIPCIState *ich9;

        /* ahci and SATA device, for q35 1 ahci controller is built-in */
        pdev = pci_create_simple_multifunction(pcms->pcibus,
                                               PCI_DEVFN(ICH9_SATA1_DEV,
                                                         ICH9_SATA1_FUNC),
                                               "ich9-ahci");
        ich9 = ICH9_AHCI(pdev);
        pcms->idebus[0] = qdev_get_child_bus(DEVICE(pdev), "ide.0");
        pcms->idebus[1] = qdev_get_child_bus(DEVICE(pdev), "ide.1");
        g_assert(MAX_SATA_PORTS == ich9->ahci.ports);
        ide_drive_get(hd, ich9->ahci.ports);
        ahci_ide_create_devs(&ich9->ahci, hd);
    }

    if (machine_usb(machine)) {
        /* Should we create 6 UHCI according to ich9 spec? */
        ehci_create_ich9_with_companions(pcms->pcibus, 0x1d);
    }

    if (pcms->smbus_enabled) {
        PCIDevice *smb;

        /* TODO: Populate SPD eeprom data.  */
        smb = pci_create_simple_multifunction(pcms->pcibus,
                                              PCI_DEVFN(ICH9_SMB_DEV,
                                                        ICH9_SMB_FUNC),
                                              TYPE_ICH9_SMB_DEVICE);
        pcms->smbus = I2C_BUS(qdev_get_child_bus(DEVICE(smb), "i2c"));

        smbus_eeprom_init(pcms->smbus, 8, NULL, 0);
    }

    pc_cmos_init(pcms, x86ms->rtc);

    /* the rest devices to which pci devfn is automatically assigned */
    pc_vga_init(isa_bus, pcms->pcibus);
    pc_nic_init(pcmc, isa_bus, pcms->pcibus);

    if (machine->nvdimms_state->is_enabled) {
        nvdimm_init_acpi_state(machine->nvdimms_state, system_io,
                               x86_nvdimm_acpi_dsmio,
                               x86ms->fw_cfg, OBJECT(pcms));
    }
}

#define DEFINE_Q35_MACHINE(suffix, name, compatfn, optionfn) \
    static void pc_init_##suffix(MachineState *machine) \
    { \
        void (*compat)(MachineState *m) = (compatfn); \
        if (compat) { \
            compat(machine); \
        } \
        pc_q35_init(machine); \
    } \
    DEFINE_PC_MACHINE(suffix, name, pc_init_##suffix, optionfn)


static void pc_q35_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pcmc->pci_root_uid = 0;
    pcmc->default_cpu_version = 1;

    m->family = "pc_q35";
    m->desc = "Standard PC (Q35 + ICH9, 2009)";
    m->units_per_default_bus = 1;
    m->default_machine_opts = "firmware=bios-256k.bin";
    m->default_display = "std";
    m->default_nic = "e1000e";
    m->default_kernel_irqchip_split = false;
    m->no_floppy = 1;
    m->max_cpus = 1024;
    m->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_AMD_IOMMU_DEVICE);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_INTEL_IOMMU_DEVICE);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_RAMFB_DEVICE);
    machine_class_allow_dynamic_sysbus_dev(m, TYPE_VMBUS_BRIDGE);
}

static void pc_q35_9_0_machine_options(MachineClass *m)
{
    pc_q35_machine_options(m);
    m->alias = "q35";
}

DEFINE_Q35_MACHINE(v9_0, "pc-q35-9.0", NULL,
                   pc_q35_9_0_machine_options);

static void pc_q35_8_2_machine_options(MachineClass *m)
{
    pc_q35_9_0_machine_options(m);
    m->alias = NULL;
    compat_props_add(m->compat_props, hw_compat_8_2, hw_compat_8_2_len);
    compat_props_add(m->compat_props, pc_compat_8_2, pc_compat_8_2_len);
}

DEFINE_Q35_MACHINE(v8_2, "pc-q35-8.2", NULL,
                   pc_q35_8_2_machine_options);

static void pc_q35_8_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pc_q35_8_2_machine_options(m);
    m->alias = NULL;
    pcmc->broken_32bit_mem_addr_check = true;
    compat_props_add(m->compat_props, hw_compat_8_1, hw_compat_8_1_len);
    compat_props_add(m->compat_props, pc_compat_8_1, pc_compat_8_1_len);
}

DEFINE_Q35_MACHINE(v8_1, "pc-q35-8.1", NULL,
                   pc_q35_8_1_machine_options);

static void pc_q35_8_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_q35_8_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_8_0, hw_compat_8_0_len);
    compat_props_add(m->compat_props, pc_compat_8_0, pc_compat_8_0_len);

    /* For pc-q35-8.0 and older, use SMBIOS 2.8 by default */
    pcmc->default_smbios_ep_type = SMBIOS_ENTRY_POINT_TYPE_32;
    m->max_cpus = 288;
}

DEFINE_Q35_MACHINE(v8_0, "pc-q35-8.0", NULL,
                   pc_q35_8_0_machine_options);

static void pc_q35_7_2_machine_options(MachineClass *m)
{
    pc_q35_8_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_7_2, hw_compat_7_2_len);
    compat_props_add(m->compat_props, pc_compat_7_2, pc_compat_7_2_len);
}

DEFINE_Q35_MACHINE(v7_2, "pc-q35-7.2", NULL,
                   pc_q35_7_2_machine_options);

static void pc_q35_7_1_machine_options(MachineClass *m)
{
    pc_q35_7_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_7_1, hw_compat_7_1_len);
    compat_props_add(m->compat_props, pc_compat_7_1, pc_compat_7_1_len);
}

DEFINE_Q35_MACHINE(v7_1, "pc-q35-7.1", NULL,
                   pc_q35_7_1_machine_options);

static void pc_q35_7_0_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pc_q35_7_1_machine_options(m);
    pcmc->enforce_amd_1tb_hole = false;
    compat_props_add(m->compat_props, hw_compat_7_0, hw_compat_7_0_len);
    compat_props_add(m->compat_props, pc_compat_7_0, pc_compat_7_0_len);
}

DEFINE_Q35_MACHINE(v7_0, "pc-q35-7.0", NULL,
                   pc_q35_7_0_machine_options);

static void pc_q35_6_2_machine_options(MachineClass *m)
{
    pc_q35_7_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_2, hw_compat_6_2_len);
    compat_props_add(m->compat_props, pc_compat_6_2, pc_compat_6_2_len);
}

DEFINE_Q35_MACHINE(v6_2, "pc-q35-6.2", NULL,
                   pc_q35_6_2_machine_options);

static void pc_q35_6_1_machine_options(MachineClass *m)
{
    pc_q35_6_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_1, hw_compat_6_1_len);
    compat_props_add(m->compat_props, pc_compat_6_1, pc_compat_6_1_len);
    m->smp_props.prefer_sockets = true;
}

DEFINE_Q35_MACHINE(v6_1, "pc-q35-6.1", NULL,
                   pc_q35_6_1_machine_options);

static void pc_q35_6_0_machine_options(MachineClass *m)
{
    pc_q35_6_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_6_0, hw_compat_6_0_len);
    compat_props_add(m->compat_props, pc_compat_6_0, pc_compat_6_0_len);
}

DEFINE_Q35_MACHINE(v6_0, "pc-q35-6.0", NULL,
                   pc_q35_6_0_machine_options);

static void pc_q35_5_2_machine_options(MachineClass *m)
{
    pc_q35_6_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_5_2, hw_compat_5_2_len);
    compat_props_add(m->compat_props, pc_compat_5_2, pc_compat_5_2_len);
}

DEFINE_Q35_MACHINE(v5_2, "pc-q35-5.2", NULL,
                   pc_q35_5_2_machine_options);

static void pc_q35_5_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_q35_5_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_5_1, hw_compat_5_1_len);
    compat_props_add(m->compat_props, pc_compat_5_1, pc_compat_5_1_len);
    pcmc->kvmclock_create_always = false;
    pcmc->pci_root_uid = 1;
}

DEFINE_Q35_MACHINE(v5_1, "pc-q35-5.1", NULL,
                   pc_q35_5_1_machine_options);

static void pc_q35_5_0_machine_options(MachineClass *m)
{
    pc_q35_5_1_machine_options(m);
    m->numa_mem_supported = true;
    compat_props_add(m->compat_props, hw_compat_5_0, hw_compat_5_0_len);
    compat_props_add(m->compat_props, pc_compat_5_0, pc_compat_5_0_len);
    m->auto_enable_numa_with_memdev = false;
}

DEFINE_Q35_MACHINE(v5_0, "pc-q35-5.0", NULL,
                   pc_q35_5_0_machine_options);

static void pc_q35_4_2_machine_options(MachineClass *m)
{
    pc_q35_5_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_4_2, hw_compat_4_2_len);
    compat_props_add(m->compat_props, pc_compat_4_2, pc_compat_4_2_len);
}

DEFINE_Q35_MACHINE(v4_2, "pc-q35-4.2", NULL,
                   pc_q35_4_2_machine_options);

static void pc_q35_4_1_machine_options(MachineClass *m)
{
    pc_q35_4_2_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_4_1, hw_compat_4_1_len);
    compat_props_add(m->compat_props, pc_compat_4_1, pc_compat_4_1_len);
}

DEFINE_Q35_MACHINE(v4_1, "pc-q35-4.1", NULL,
                   pc_q35_4_1_machine_options);

static void pc_q35_4_0_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    pc_q35_4_1_machine_options(m);
    pcmc->default_cpu_version = CPU_VERSION_LEGACY;
    /*
     * This is the default machine for the 4.0-stable branch. It is basically
     * a 4.0 that doesn't use split irqchip by default. It MUST hence apply the
     * 4.0 compat props.
     */
    compat_props_add(m->compat_props, hw_compat_4_0, hw_compat_4_0_len);
    compat_props_add(m->compat_props, pc_compat_4_0, pc_compat_4_0_len);
}

DEFINE_Q35_MACHINE(v4_0_1, "pc-q35-4.0.1", NULL,
                   pc_q35_4_0_1_machine_options);

static void pc_q35_4_0_machine_options(MachineClass *m)
{
    pc_q35_4_0_1_machine_options(m);
    m->default_kernel_irqchip_split = true;
    /* Compat props are applied by the 4.0.1 machine */
}

DEFINE_Q35_MACHINE(v4_0, "pc-q35-4.0", NULL,
                   pc_q35_4_0_machine_options);

static void pc_q35_3_1_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_q35_4_0_machine_options(m);
    m->default_kernel_irqchip_split = false;
    m->smbus_no_migration_support = true;
    pcmc->pvh_enabled = false;
    compat_props_add(m->compat_props, hw_compat_3_1, hw_compat_3_1_len);
    compat_props_add(m->compat_props, pc_compat_3_1, pc_compat_3_1_len);
}

DEFINE_Q35_MACHINE(v3_1, "pc-q35-3.1", NULL,
                   pc_q35_3_1_machine_options);

static void pc_q35_3_0_machine_options(MachineClass *m)
{
    pc_q35_3_1_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_3_0, hw_compat_3_0_len);
    compat_props_add(m->compat_props, pc_compat_3_0, pc_compat_3_0_len);
}

DEFINE_Q35_MACHINE(v3_0, "pc-q35-3.0", NULL,
                    pc_q35_3_0_machine_options);

static void pc_q35_2_12_machine_options(MachineClass *m)
{
    pc_q35_3_0_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_12, hw_compat_2_12_len);
    compat_props_add(m->compat_props, pc_compat_2_12, pc_compat_2_12_len);
}

DEFINE_Q35_MACHINE(v2_12, "pc-q35-2.12", NULL,
                   pc_q35_2_12_machine_options);

static void pc_q35_2_11_machine_options(MachineClass *m)
{
    pc_q35_2_12_machine_options(m);
    m->default_nic = "e1000";
    compat_props_add(m->compat_props, hw_compat_2_11, hw_compat_2_11_len);
    compat_props_add(m->compat_props, pc_compat_2_11, pc_compat_2_11_len);
}

DEFINE_Q35_MACHINE(v2_11, "pc-q35-2.11", NULL,
                   pc_q35_2_11_machine_options);

static void pc_q35_2_10_machine_options(MachineClass *m)
{
    pc_q35_2_11_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_10, hw_compat_2_10_len);
    compat_props_add(m->compat_props, pc_compat_2_10, pc_compat_2_10_len);
    m->auto_enable_numa_with_memhp = false;
}

DEFINE_Q35_MACHINE(v2_10, "pc-q35-2.10", NULL,
                   pc_q35_2_10_machine_options);

static void pc_q35_2_9_machine_options(MachineClass *m)
{
    pc_q35_2_10_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_9, hw_compat_2_9_len);
    compat_props_add(m->compat_props, pc_compat_2_9, pc_compat_2_9_len);
}

DEFINE_Q35_MACHINE(v2_9, "pc-q35-2.9", NULL,
                   pc_q35_2_9_machine_options);

static void pc_q35_2_8_machine_options(MachineClass *m)
{
    pc_q35_2_9_machine_options(m);
    compat_props_add(m->compat_props, hw_compat_2_8, hw_compat_2_8_len);
    compat_props_add(m->compat_props, pc_compat_2_8, pc_compat_2_8_len);
}

DEFINE_Q35_MACHINE(v2_8, "pc-q35-2.8", NULL,
                   pc_q35_2_8_machine_options);

static void pc_q35_2_7_machine_options(MachineClass *m)
{
    pc_q35_2_8_machine_options(m);
    m->max_cpus = 255;
    compat_props_add(m->compat_props, hw_compat_2_7, hw_compat_2_7_len);
    compat_props_add(m->compat_props, pc_compat_2_7, pc_compat_2_7_len);
}

DEFINE_Q35_MACHINE(v2_7, "pc-q35-2.7", NULL,
                   pc_q35_2_7_machine_options);

static void pc_q35_2_6_machine_options(MachineClass *m)
{
    X86MachineClass *x86mc = X86_MACHINE_CLASS(m);
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_q35_2_7_machine_options(m);
    pcmc->legacy_cpu_hotplug = true;
    x86mc->fwcfg_dma_enabled = false;
    compat_props_add(m->compat_props, hw_compat_2_6, hw_compat_2_6_len);
    compat_props_add(m->compat_props, pc_compat_2_6, pc_compat_2_6_len);
}

DEFINE_Q35_MACHINE(v2_6, "pc-q35-2.6", NULL,
                   pc_q35_2_6_machine_options);

static void pc_q35_2_5_machine_options(MachineClass *m)
{
    X86MachineClass *x86mc = X86_MACHINE_CLASS(m);

    pc_q35_2_6_machine_options(m);
    x86mc->save_tsc_khz = false;
    m->legacy_fw_cfg_order = 1;
    compat_props_add(m->compat_props, hw_compat_2_5, hw_compat_2_5_len);
    compat_props_add(m->compat_props, pc_compat_2_5, pc_compat_2_5_len);
}

DEFINE_Q35_MACHINE(v2_5, "pc-q35-2.5", NULL,
                   pc_q35_2_5_machine_options);

static void pc_q35_2_4_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    pc_q35_2_5_machine_options(m);
    m->hw_version = "2.4.0";
    pcmc->broken_reserved_end = true;
    compat_props_add(m->compat_props, hw_compat_2_4, hw_compat_2_4_len);
    compat_props_add(m->compat_props, pc_compat_2_4, pc_compat_2_4_len);
}

DEFINE_Q35_MACHINE(v2_4, "pc-q35-2.4", NULL,
                   pc_q35_2_4_machine_options);
