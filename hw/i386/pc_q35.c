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
#include "hw/hw.h"
#include "hw/loader.h"
#include "sysemu/arch_init.h"
#include "hw/i2c/smbus.h"
#include "hw/boards.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/xen/xen.h"
#include "sysemu/kvm.h"
#include "hw/kvm/clock.h"
#include "hw/pci-host/q35.h"
#include "exec/address-spaces.h"
#include "hw/i386/ich9.h"
#include "hw/i386/smbios.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"
#include "hw/usb.h"
#include "hw/cpu/icc_bus.h"
#include "qemu/error-report.h"
#include "migration/migration.h"

/* ICH9 AHCI has 6 ports */
#define MAX_SATA_PORTS     6

static bool has_acpi_build = true;
static bool rsdp_in_ram = true;
static bool smbios_defaults = true;
static bool smbios_legacy_mode;
static bool smbios_uuid_encoded = true;
/* Make sure that guest addresses aligned at 1Gbyte boundaries get mapped to
 * host addresses aligned at 1Gbyte boundaries.  This way we can use 1GByte
 * pages in the host.
 */
static bool gigabyte_align = true;
static bool has_reserved_memory = true;

/* PC hardware initialisation */
static void pc_q35_init(MachineState *machine)
{
    PCMachineState *pc_machine = PC_MACHINE(machine);
    ram_addr_t below_4g_mem_size, above_4g_mem_size;
    Q35PCIHost *q35_host;
    PCIHostState *phb;
    PCIBus *host_bus;
    PCIDevice *lpc;
    BusState *idebus[MAX_SATA_PORTS];
    ISADevice *rtc_state;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;
    MemoryRegion *ram_memory;
    GSIState *gsi_state;
    ISABus *isa_bus;
    int pci_enabled = 1;
    qemu_irq *gsi;
    qemu_irq *i8259;
    int i;
    ICH9LPCState *ich9_lpc;
    PCIDevice *ahci;
    DeviceState *icc_bridge;
    PcGuestInfo *guest_info;
    ram_addr_t lowmem;
    DriveInfo *hd[MAX_SATA_PORTS];
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    /* Check whether RAM fits below 4G (leaving 1/2 GByte for IO memory
     * and 256 Mbytes for PCI Express Enhanced Configuration Access Mapping
     * also known as MMCFG).
     * If it doesn't, we need to split it in chunks below and above 4G.
     * In any case, try to make sure that guest addresses aligned at
     * 1G boundaries get mapped to host addresses aligned at 1G boundaries.
     * For old machine types, use whatever split we used historically to avoid
     * breaking migration.
     */
    if (machine->ram_size >= 0xb0000000) {
        lowmem = gigabyte_align ? 0x80000000 : 0xb0000000;
    } else {
        lowmem = 0xb0000000;
    }

    /* Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(qemu limit, user limit).
     */
    if (lowmem > pc_machine->max_ram_below_4g) {
        lowmem = pc_machine->max_ram_below_4g;
        if (machine->ram_size - lowmem > lowmem &&
            lowmem & ((1ULL << 30) - 1)) {
            error_report("Warning: Large machine and max_ram_below_4g(%"PRIu64
                         ") not a multiple of 1G; possible bad performance.",
                         pc_machine->max_ram_below_4g);
        }
    }

    if (machine->ram_size >= lowmem) {
        above_4g_mem_size = machine->ram_size - lowmem;
        below_4g_mem_size = lowmem;
    } else {
        above_4g_mem_size = 0;
        below_4g_mem_size = machine->ram_size;
    }

    if (xen_enabled() && xen_hvm_init(&below_4g_mem_size, &above_4g_mem_size,
                                      &ram_memory) != 0) {
        fprintf(stderr, "xen hardware virtual machine initialisation failed\n");
        exit(1);
    }

    icc_bridge = qdev_create(NULL, TYPE_ICC_BRIDGE);
    object_property_add_child(qdev_get_machine(), "icc-bridge",
                              OBJECT(icc_bridge), NULL);

    pc_cpus_init(machine->cpu_model, icc_bridge);
    pc_acpi_init("q35-acpi-dsdt.aml");

    kvmclock_create();

    /* pci enabled */
    if (pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = get_system_memory();
    }

    guest_info = pc_guest_info_init(below_4g_mem_size, above_4g_mem_size);
    guest_info->isapc_ram_fw = false;
    guest_info->has_acpi_build = has_acpi_build;
    guest_info->has_reserved_memory = has_reserved_memory;
    guest_info->rsdp_in_ram = rsdp_in_ram;

    /* Migration was not supported in 2.0 for Q35, so do not bother
     * with this hack (see hw/i386/acpi-build.c).
     */
    guest_info->legacy_acpi_table_size = 0;

    if (smbios_defaults) {
        /* These values are guest ABI, do not change */
        smbios_set_defaults("QEMU", "Standard PC (Q35 + ICH9, 2009)",
                            mc->name, smbios_legacy_mode, smbios_uuid_encoded);
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(machine, get_system_memory(),
                       below_4g_mem_size, above_4g_mem_size,
                       rom_memory, &ram_memory, guest_info);
    }

    /* irq lines */
    gsi_state = g_malloc0(sizeof(*gsi_state));
    if (kvm_irqchip_in_kernel()) {
        kvm_pc_setup_irq_routing(pci_enabled);
        gsi = qemu_allocate_irqs(kvm_pc_gsi_handler, gsi_state,
                                 GSI_NUM_PINS);
    } else {
        gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);
    }

    /* create pci host bus */
    q35_host = Q35_HOST_DEVICE(qdev_create(NULL, TYPE_Q35_HOST_DEVICE));

    object_property_add_child(qdev_get_machine(), "q35", OBJECT(q35_host), NULL);
    q35_host->mch.ram_memory = ram_memory;
    q35_host->mch.pci_address_space = pci_memory;
    q35_host->mch.system_memory = get_system_memory();
    q35_host->mch.address_space_io = get_system_io();
    q35_host->mch.below_4g_mem_size = below_4g_mem_size;
    q35_host->mch.above_4g_mem_size = above_4g_mem_size;
    q35_host->mch.guest_info = guest_info;
    /* pci */
    qdev_init_nofail(DEVICE(q35_host));
    phb = PCI_HOST_BRIDGE(q35_host);
    host_bus = phb->bus;
    /* create ISA bus */
    lpc = pci_create_simple_multifunction(host_bus, PCI_DEVFN(ICH9_LPC_DEV,
                                          ICH9_LPC_FUNC), true,
                                          TYPE_ICH9_LPC_DEVICE);

    object_property_add_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                             TYPE_HOTPLUG_HANDLER,
                             (Object **)&pc_machine->acpi_dev,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, &error_abort);
    object_property_set_link(OBJECT(machine), OBJECT(lpc),
                             PC_MACHINE_ACPI_DEVICE_PROP, &error_abort);

    ich9_lpc = ICH9_LPC_DEVICE(lpc);
    ich9_lpc->pic = gsi;
    ich9_lpc->ioapic = gsi_state->ioapic_irq;
    pci_bus_irqs(host_bus, ich9_lpc_set_irq, ich9_lpc_map_irq, ich9_lpc,
                 ICH9_LPC_NB_PIRQS);
    pci_bus_set_route_irq_fn(host_bus, ich9_route_intx_pin_to_irq);
    isa_bus = ich9_lpc->isa_bus;

    /*end early*/
    isa_bus_irqs(isa_bus, gsi);

    if (kvm_irqchip_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    } else if (xen_enabled()) {
        i8259 = xen_interrupt_controller_init();
    } else {
        i8259 = i8259_init(isa_bus, pc_allocate_cpu_irq());
    }

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    if (pci_enabled) {
        ioapic_init_gsi(gsi_state, "q35");
    }
    qdev_init_nofail(icc_bridge);

    pc_register_ferr_irq(gsi[13]);

    assert(pc_machine->vmport != ON_OFF_AUTO_MAX);
    if (pc_machine->vmport == ON_OFF_AUTO_AUTO) {
        pc_machine->vmport = xen_enabled() ? ON_OFF_AUTO_OFF : ON_OFF_AUTO_ON;
    }

    /* init basic PC hardware */
    pc_basic_device_init(isa_bus, gsi, &rtc_state, !mc->no_floppy,
                         (pc_machine->vmport != ON_OFF_AUTO_ON), 0xff0104);

    /* connect pm stuff to lpc */
    ich9_lpc_pm_init(lpc, pc_machine_is_smm_enabled(pc_machine), !mc->no_tco);

    /* ahci and SATA device, for q35 1 ahci controller is built-in */
    ahci = pci_create_simple_multifunction(host_bus,
                                           PCI_DEVFN(ICH9_SATA1_DEV,
                                                     ICH9_SATA1_FUNC),
                                           true, "ich9-ahci");
    idebus[0] = qdev_get_child_bus(&ahci->qdev, "ide.0");
    idebus[1] = qdev_get_child_bus(&ahci->qdev, "ide.1");
    g_assert(MAX_SATA_PORTS == ICH_AHCI(ahci)->ahci.ports);
    ide_drive_get(hd, ICH_AHCI(ahci)->ahci.ports);
    ahci_ide_create_devs(ahci, hd);

    if (usb_enabled()) {
        /* Should we create 6 UHCI according to ich9 spec? */
        ehci_create_ich9_with_companions(host_bus, 0x1d);
    }

    /* TODO: Populate SPD eeprom data.  */
    smbus_eeprom_init(ich9_smb_init(host_bus,
                                    PCI_DEVFN(ICH9_SMB_DEV, ICH9_SMB_FUNC),
                                    0xb100),
                      8, NULL, 0);

    pc_cmos_init(below_4g_mem_size, above_4g_mem_size, machine->boot_order,
                 machine, idebus[0], idebus[1], rtc_state);

    /* the rest devices to which pci devfn is automatically assigned */
    pc_vga_init(isa_bus, host_bus);
    pc_nic_init(isa_bus, host_bus);
    if (pci_enabled) {
        pc_pci_device_init(host_bus);
    }
}

static void pc_compat_2_3(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    savevm_skip_section_footers();
    if (kvm_enabled()) {
        pcms->smm = ON_OFF_AUTO_OFF;
    }
    global_state_set_optional();
    savevm_skip_configuration();
}

static void pc_compat_2_2(MachineState *machine)
{
    pc_compat_2_3(machine);
    rsdp_in_ram = false;
    x86_cpu_compat_set_features("kvm64", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("kvm32", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Conroe", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Penryn", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Nehalem", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Westmere", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("SandyBridge", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Haswell", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Broadwell", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Opteron_G1", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Opteron_G2", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Opteron_G3", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Opteron_G4", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Opteron_G5", FEAT_1_EDX, 0, CPUID_VME);
    x86_cpu_compat_set_features("Haswell", FEAT_1_ECX, 0, CPUID_EXT_F16C);
    x86_cpu_compat_set_features("Haswell", FEAT_1_ECX, 0, CPUID_EXT_RDRAND);
    x86_cpu_compat_set_features("Broadwell", FEAT_1_ECX, 0, CPUID_EXT_F16C);
    x86_cpu_compat_set_features("Broadwell", FEAT_1_ECX, 0, CPUID_EXT_RDRAND);
    machine->suppress_vmdesc = true;
}

static void pc_compat_2_1(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);

    pc_compat_2_2(machine);
    pcms->enforce_aligned_dimm = false;
    smbios_uuid_encoded = false;
    x86_cpu_compat_set_features("coreduo", FEAT_1_ECX, CPUID_EXT_VMX, 0);
    x86_cpu_compat_set_features("core2duo", FEAT_1_ECX, CPUID_EXT_VMX, 0);
    x86_cpu_compat_kvm_no_autodisable(FEAT_8000_0001_ECX, CPUID_EXT3_SVM);
}

static void pc_compat_2_0(MachineState *machine)
{
    pc_compat_2_1(machine);
    smbios_legacy_mode = true;
    has_reserved_memory = false;
    pc_set_legacy_acpi_data_size();
}

static void pc_compat_1_7(MachineState *machine)
{
    pc_compat_2_0(machine);
    smbios_defaults = false;
    gigabyte_align = false;
    option_rom_has_mr = true;
    x86_cpu_compat_kvm_no_autoenable(FEAT_1_ECX, CPUID_EXT_X2APIC);
}

static void pc_compat_1_6(MachineState *machine)
{
    pc_compat_1_7(machine);
    rom_file_has_mr = false;
    has_acpi_build = false;
}

static void pc_compat_1_5(MachineState *machine)
{
    pc_compat_1_6(machine);
}

static void pc_compat_1_4(MachineState *machine)
{
    pc_compat_1_5(machine);
    x86_cpu_compat_set_features("n270", FEAT_1_ECX, 0, CPUID_EXT_MOVBE);
    x86_cpu_compat_set_features("Westmere", FEAT_1_ECX, 0, CPUID_EXT_PCLMULQDQ);
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
    pc_default_machine_options(m);
    m->family = "pc_q35";
    m->desc = "Standard PC (Q35 + ICH9, 2009)";
    m->hot_add_cpu = pc_hot_add_cpu;
    m->units_per_default_bus = 1;
}

static void pc_q35_2_4_machine_options(MachineClass *m)
{
    pc_q35_machine_options(m);
    m->default_machine_opts = "firmware=bios-256k.bin";
    m->default_display = "std";
    m->no_floppy = 1;
    m->no_tco = 0;
    m->alias = "q35";
}

DEFINE_Q35_MACHINE(v2_4, "pc-q35-2.4", NULL,
                   pc_q35_2_4_machine_options);


static void pc_q35_2_3_machine_options(MachineClass *m)
{
    pc_q35_2_4_machine_options(m);
    m->no_floppy = 0;
    m->no_tco = 1;
    m->alias = NULL;
    SET_MACHINE_COMPAT(m, PC_COMPAT_2_3);
}

DEFINE_Q35_MACHINE(v2_3, "pc-q35-2.3", pc_compat_2_3,
                   pc_q35_2_3_machine_options);


static void pc_q35_2_2_machine_options(MachineClass *m)
{
    pc_q35_2_3_machine_options(m);
    SET_MACHINE_COMPAT(m, PC_COMPAT_2_2);
}

DEFINE_Q35_MACHINE(v2_2, "pc-q35-2.2", pc_compat_2_2,
                   pc_q35_2_2_machine_options);


static void pc_q35_2_1_machine_options(MachineClass *m)
{
    pc_q35_2_2_machine_options(m);
    m->default_display = NULL;
    SET_MACHINE_COMPAT(m, PC_COMPAT_2_1);
}

DEFINE_Q35_MACHINE(v2_1, "pc-q35-2.1", pc_compat_2_1,
                   pc_q35_2_1_machine_options);


static void pc_q35_2_0_machine_options(MachineClass *m)
{
    pc_q35_2_1_machine_options(m);
    SET_MACHINE_COMPAT(m, PC_COMPAT_2_0);
}

DEFINE_Q35_MACHINE(v2_0, "pc-q35-2.0", pc_compat_2_0,
                   pc_q35_2_0_machine_options);


static void pc_q35_1_7_machine_options(MachineClass *m)
{
    pc_q35_2_0_machine_options(m);
    m->default_machine_opts = NULL;
    SET_MACHINE_COMPAT(m, PC_COMPAT_1_7);
}

DEFINE_Q35_MACHINE(v1_7, "pc-q35-1.7", pc_compat_1_7,
                   pc_q35_1_7_machine_options);


static void pc_q35_1_6_machine_options(MachineClass *m)
{
    pc_q35_machine_options(m);
    SET_MACHINE_COMPAT(m, PC_COMPAT_1_6);
}

DEFINE_Q35_MACHINE(v1_6, "pc-q35-1.6", pc_compat_1_6,
                   pc_q35_1_6_machine_options);


static void pc_q35_1_5_machine_options(MachineClass *m)
{
    pc_q35_1_6_machine_options(m);
    SET_MACHINE_COMPAT(m, PC_COMPAT_1_5);
}

DEFINE_Q35_MACHINE(v1_5, "pc-q35-1.5", pc_compat_1_5,
                   pc_q35_1_5_machine_options);


static void pc_q35_1_4_machine_options(MachineClass *m)
{
    pc_q35_1_5_machine_options(m);
    m->hot_add_cpu = NULL;
    SET_MACHINE_COMPAT(m, PC_COMPAT_1_4);
}

DEFINE_Q35_MACHINE(v1_4, "pc-q35-1.4", pc_compat_1_4,
                   pc_q35_1_4_machine_options);
