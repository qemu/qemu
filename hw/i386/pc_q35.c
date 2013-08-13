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
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"
#include "hw/usb.h"
#include "hw/cpu/icc_bus.h"

/* ICH9 AHCI has 6 ports */
#define MAX_SATA_PORTS     6

static bool has_pvpanic = true;

/* PC hardware initialisation */
static void pc_q35_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    const char *boot_device = args->boot_device;
    ram_addr_t below_4g_mem_size, above_4g_mem_size;
    Q35PCIHost *q35_host;
    PCIBus *host_bus;
    PCIDevice *lpc;
    BusState *idebus[MAX_SATA_PORTS];
    ISADevice *rtc_state;
    ISADevice *floppy;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;
    MemoryRegion *ram_memory;
    GSIState *gsi_state;
    ISABus *isa_bus;
    int pci_enabled = 1;
    qemu_irq *cpu_irq;
    qemu_irq *gsi;
    qemu_irq *i8259;
    int i;
    ICH9LPCState *ich9_lpc;
    PCIDevice *ahci;
    DeviceState *icc_bridge;

    icc_bridge = qdev_create(NULL, TYPE_ICC_BRIDGE);
    object_property_add_child(qdev_get_machine(), "icc-bridge",
                              OBJECT(icc_bridge), NULL);

    pc_cpus_init(cpu_model, icc_bridge);
    pc_acpi_init("q35-acpi-dsdt.aml");

    kvmclock_create();

    if (ram_size >= 0xb0000000) {
        above_4g_mem_size = ram_size - 0xb0000000;
        below_4g_mem_size = 0xb0000000;
    } else {
        above_4g_mem_size = 0;
        below_4g_mem_size = ram_size;
    }

    /* pci enabled */
    if (pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, "pci", INT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = get_system_memory();
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(get_system_memory(), kernel_filename, kernel_cmdline,
                       initrd_filename, below_4g_mem_size, above_4g_mem_size,
                       rom_memory, &ram_memory);
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

    q35_host->mch.ram_memory = ram_memory;
    q35_host->mch.pci_address_space = pci_memory;
    q35_host->mch.system_memory = get_system_memory();
    q35_host->mch.address_space_io = get_system_io();
    q35_host->mch.below_4g_mem_size = below_4g_mem_size;
    q35_host->mch.above_4g_mem_size = above_4g_mem_size;
    /* pci */
    qdev_init_nofail(DEVICE(q35_host));
    host_bus = q35_host->host.pci.bus;
    /* create ISA bus */
    lpc = pci_create_simple_multifunction(host_bus, PCI_DEVFN(ICH9_LPC_DEV,
                                          ICH9_LPC_FUNC), true,
                                          TYPE_ICH9_LPC_DEVICE);
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
        cpu_irq = pc_allocate_cpu_irq();
        i8259 = i8259_init(isa_bus, cpu_irq[0]);
    }

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    if (pci_enabled) {
        ioapic_init_gsi(gsi_state, NULL);
    }
    qdev_init_nofail(icc_bridge);

    pc_register_ferr_irq(gsi[13]);

    /* init basic PC hardware */
    pc_basic_device_init(isa_bus, gsi, &rtc_state, &floppy, false);

    /* connect pm stuff to lpc */
    ich9_lpc_pm_init(lpc);

    /* ahci and SATA device, for q35 1 ahci controller is built-in */
    ahci = pci_create_simple_multifunction(host_bus,
                                           PCI_DEVFN(ICH9_SATA1_DEV,
                                                     ICH9_SATA1_FUNC),
                                           true, "ich9-ahci");
    idebus[0] = qdev_get_child_bus(&ahci->qdev, "ide.0");
    idebus[1] = qdev_get_child_bus(&ahci->qdev, "ide.1");

    if (usb_enabled(false)) {
        /* Should we create 6 UHCI according to ich9 spec? */
        ehci_create_ich9_with_companions(host_bus, 0x1d);
    }

    /* TODO: Populate SPD eeprom data.  */
    smbus_eeprom_init(ich9_smb_init(host_bus,
                                    PCI_DEVFN(ICH9_SMB_DEV, ICH9_SMB_FUNC),
                                    0xb100),
                      8, NULL, 0);

    pc_cmos_init(below_4g_mem_size, above_4g_mem_size, boot_device,
                 floppy, idebus[0], idebus[1], rtc_state);

    /* the rest devices to which pci devfn is automatically assigned */
    pc_vga_init(isa_bus, host_bus);
    pc_nic_init(isa_bus, host_bus);
    if (pci_enabled) {
        pc_pci_device_init(host_bus);
    }

    if (has_pvpanic) {
        pvpanic_init(isa_bus);
    }
}

static void pc_q35_init_1_4(QEMUMachineInitArgs *args)
{
    has_pvpanic = false;
    x86_cpu_compat_set_features("n270", FEAT_1_ECX, 0, CPUID_EXT_MOVBE);
    x86_cpu_compat_set_features("Westmere", FEAT_1_ECX, 0, CPUID_EXT_PCLMULQDQ);
    pc_q35_init(args);
}

static QEMUMachine pc_q35_machine_v1_5 = {
    .name = "pc-q35-1.5",
    .alias = "q35",
    .desc = "Standard PC (Q35 + ICH9, 2009)",
    .init = pc_q35_init,
    .hot_add_cpu = pc_hot_add_cpu,
    .max_cpus = 255,
    DEFAULT_MACHINE_OPTIONS,
};

static QEMUMachine pc_q35_machine_v1_4 = {
    .name = "pc-q35-1.4",
    .desc = "Standard PC (Q35 + ICH9, 2009)",
    .init = pc_q35_init_1_4,
    .max_cpus = 255,
    .compat_props = (GlobalProperty[]) {
        PC_COMPAT_1_4,
        { /* end of list */ }
    },
    DEFAULT_MACHINE_OPTIONS,
};

static void pc_q35_machine_init(void)
{
    qemu_register_machine(&pc_q35_machine_v1_5);
    qemu_register_machine(&pc_q35_machine_v1_4);
}

machine_init(pc_q35_machine_init);
