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

#include <glib.h>

#include "hw.h"
#include "pc.h"
#include "apic.h"
#include "pci.h"
#include "usb-uhci.h"
#include "usb-ohci.h"
#include "net.h"
#include "boards.h"
#include "ide.h"
#include "kvm.h"
#include "kvmclock.h"
#include "sysemu.h"
#include "sysbus.h"
#include "arch_init.h"
#include "blockdev.h"
#include "smbus.h"
#include "xen.h"
#include "memory.h"
#include "exec-memory.h"
#ifdef CONFIG_XEN
#  include <xen/hvm/hvm_info_table.h>
#endif

#define MAX_IDE_BUS 2

static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };

static void ioapic_init(IsaIrqState *isa_irq_state)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    dev = qdev_create(NULL, "ioapic");
    qdev_init_nofail(dev);
    d = sysbus_from_qdev(dev);
    sysbus_mmio_map(d, 0, 0xfec00000);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        isa_irq_state->ioapic[i] = qdev_get_gpio_in(dev, i);
    }
}

/* PC hardware initialisation */
static void pc_init1(MemoryRegion *system_memory,
                     MemoryRegion *system_io,
                     ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename,
                     const char *kernel_cmdline,
                     const char *initrd_filename,
                     const char *cpu_model,
                     int pci_enabled,
                     int kvmclock_enabled)
{
    int i;
    ram_addr_t below_4g_mem_size, above_4g_mem_size;
    PCIBus *pci_bus;
    PCII440FXState *i440fx_state;
    int piix3_devfn = -1;
    qemu_irq *cpu_irq;
    qemu_irq *isa_irq;
    qemu_irq *i8259;
    qemu_irq *cmos_s3;
    qemu_irq *smi_irq;
    IsaIrqState *isa_irq_state;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BusState *idebus[MAX_IDE_BUS];
    ISADevice *rtc_state;
    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;

    pc_cpus_init(cpu_model);

    if (kvmclock_enabled) {
        kvmclock_create();
    }

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        above_4g_mem_size = 0;
        below_4g_mem_size = ram_size;
    }

    if (pci_enabled) {
        pci_memory = g_new(MemoryRegion, 1);
        memory_region_init(pci_memory, "pci", INT64_MAX);
        rom_memory = pci_memory;
    } else {
        pci_memory = NULL;
        rom_memory = system_memory;
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(system_memory,
                       kernel_filename, kernel_cmdline, initrd_filename,
                       below_4g_mem_size, above_4g_mem_size,
                       pci_enabled ? rom_memory : system_memory, &ram_memory);
    }

    if (!xen_enabled()) {
        cpu_irq = pc_allocate_cpu_irq();
        i8259 = i8259_init(cpu_irq[0]);
    } else {
        i8259 = xen_interrupt_controller_init();
    }
    isa_irq_state = g_malloc0(sizeof(*isa_irq_state));
    isa_irq_state->i8259 = i8259;
    if (pci_enabled) {
        ioapic_init(isa_irq_state);
    }
    isa_irq = qemu_allocate_irqs(isa_irq_handler, isa_irq_state, 24);

    if (pci_enabled) {
        pci_bus = i440fx_init(&i440fx_state, &piix3_devfn, isa_irq,
                              system_memory, system_io, ram_size,
                              below_4g_mem_size,
                              0x100000000ULL - below_4g_mem_size,
                              0x100000000ULL + above_4g_mem_size,
                              (sizeof(target_phys_addr_t) == 4
                               ? 0
                               : ((uint64_t)1 << 62)),
                              pci_memory, ram_memory);
    } else {
        pci_bus = NULL;
        i440fx_state = NULL;
        isa_bus_new(NULL, system_io);
        no_hpet = 1;
    }
    isa_bus_irqs(isa_irq);

    pc_register_ferr_irq(isa_get_irq(13));

    pc_vga_init(pci_enabled? pci_bus: NULL);

    if (xen_enabled()) {
        pci_create_simple(pci_bus, -1, "xen-platform");
    }

    /* init basic PC hardware */
    pc_basic_device_init(isa_irq, &rtc_state, xen_enabled());

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_enabled || (nd->model && strcmp(nd->model, "ne2k_isa") == 0))
            pc_init_ne2k_isa(nd);
        else
            pci_nic_init_nofail(nd, "e1000", NULL);
    }

    ide_drive_get(hd, MAX_IDE_BUS);
    if (pci_enabled) {
        PCIDevice *dev;
        if (xen_enabled()) {
            dev = pci_piix3_xen_ide_init(pci_bus, hd, piix3_devfn + 1);
        } else {
            dev = pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1);
        }
        idebus[0] = qdev_get_child_bus(&dev->qdev, "ide.0");
        idebus[1] = qdev_get_child_bus(&dev->qdev, "ide.1");
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            ISADevice *dev;
            dev = isa_ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                               hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
            idebus[i] = qdev_get_child_bus(&dev->qdev, "ide.0");
        }
    }

    audio_init(isa_irq, pci_enabled ? pci_bus : NULL);

    pc_cmos_init(below_4g_mem_size, above_4g_mem_size, boot_device,
                 idebus[0], idebus[1], rtc_state);

    if (pci_enabled && usb_enabled) {
        usb_uhci_piix3_init(pci_bus, piix3_devfn + 2);
    }

    if (pci_enabled && acpi_enabled) {
        i2c_bus *smbus;

        if (!xen_enabled()) {
            cmos_s3 = qemu_allocate_irqs(pc_cmos_set_s3_resume, rtc_state, 1);
        } else {
            cmos_s3 = qemu_allocate_irqs(xen_cmos_set_s3_resume, rtc_state, 1);
        }
        smi_irq = qemu_allocate_irqs(pc_acpi_smi_interrupt, first_cpu, 1);
        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100,
                              isa_get_irq(9), *cmos_s3, *smi_irq,
                              kvm_enabled());
        smbus_eeprom_init(smbus, 8, NULL, 0);
    }

    if (pci_enabled) {
        pc_pci_device_init(pci_bus);
    }
}

static void pc_init_pci(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    pc_init1(get_system_memory(),
             get_system_io(),
             ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model, 1, 1);
}

static void pc_init_pci_no_kvmclock(ram_addr_t ram_size,
                                    const char *boot_device,
                                    const char *kernel_filename,
                                    const char *kernel_cmdline,
                                    const char *initrd_filename,
                                    const char *cpu_model)
{
    pc_init1(get_system_memory(),
             get_system_io(),
             ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model, 1, 0);
}

static void pc_init_isa(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    if (cpu_model == NULL)
        cpu_model = "486";
    pc_init1(get_system_memory(),
             get_system_io(),
             ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, cpu_model, 0, 1);
}

#ifdef CONFIG_XEN
static void pc_xen_hvm_init(ram_addr_t ram_size,
                            const char *boot_device,
                            const char *kernel_filename,
                            const char *kernel_cmdline,
                            const char *initrd_filename,
                            const char *cpu_model)
{
    if (xen_hvm_init() != 0) {
        hw_error("xen hardware virtual machine initialisation failed");
    }
    pc_init_pci_no_kvmclock(ram_size, boot_device,
                            kernel_filename, kernel_cmdline,
                            initrd_filename, cpu_model);
    xen_vcpu_init();
}
#endif

static QEMUMachine pc_machine = {
    .name = "pc-0.14",
    .alias = "pc",
    .desc = "Standard PC",
    .init = pc_init_pci,
    .max_cpus = 255,
    .is_default = 1,
};

static QEMUMachine pc_machine_v0_13 = {
    .name = "pc-0.13",
    .desc = "Standard PC",
    .init = pc_init_pci_no_kvmclock,
    .max_cpus = 255,
    .compat_props = (GlobalProperty[]) {
        {
            .driver   = "virtio-9p-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "VGA",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "vmware-svga",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "PCI",
            .property = "command_serr_enable",
            .value    = "off",
        },{
            .driver   = "virtio-blk-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-serial-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-net-pci",
            .property = "event_idx",
            .value    = "off",
        },
        { /* end of list */ }
    },
};

static QEMUMachine pc_machine_v0_12 = {
    .name = "pc-0.12",
    .desc = "Standard PC",
    .init = pc_init_pci_no_kvmclock,
    .max_cpus = 255,
    .compat_props = (GlobalProperty[]) {
        {
            .driver   = "virtio-serial-pci",
            .property = "max_ports",
            .value    = stringify(1),
        },{
            .driver   = "virtio-serial-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "VGA",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "vmware-svga",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "PCI",
            .property = "command_serr_enable",
            .value    = "off",
        },{
            .driver   = "virtio-blk-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-serial-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-net-pci",
            .property = "event_idx",
            .value    = "off",
        },
        { /* end of list */ }
    }
};

static QEMUMachine pc_machine_v0_11 = {
    .name = "pc-0.11",
    .desc = "Standard PC, qemu 0.11",
    .init = pc_init_pci_no_kvmclock,
    .max_cpus = 255,
    .compat_props = (GlobalProperty[]) {
        {
            .driver   = "virtio-blk-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "virtio-serial-pci",
            .property = "max_ports",
            .value    = stringify(1),
        },{
            .driver   = "virtio-serial-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "ide-drive",
            .property = "ver",
            .value    = "0.11",
        },{
            .driver   = "scsi-disk",
            .property = "ver",
            .value    = "0.11",
        },{
            .driver   = "PCI",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "PCI",
            .property = "command_serr_enable",
            .value    = "off",
        },{
            .driver   = "virtio-blk-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-serial-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-net-pci",
            .property = "event_idx",
            .value    = "off",
        },
        { /* end of list */ }
    }
};

static QEMUMachine pc_machine_v0_10 = {
    .name = "pc-0.10",
    .desc = "Standard PC, qemu 0.10",
    .init = pc_init_pci_no_kvmclock,
    .max_cpus = 255,
    .compat_props = (GlobalProperty[]) {
        {
            .driver   = "virtio-blk-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_STORAGE_OTHER),
        },{
            .driver   = "virtio-serial-pci",
            .property = "class",
            .value    = stringify(PCI_CLASS_DISPLAY_OTHER),
        },{
            .driver   = "virtio-serial-pci",
            .property = "max_ports",
            .value    = stringify(1),
        },{
            .driver   = "virtio-serial-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "virtio-net-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "virtio-blk-pci",
            .property = "vectors",
            .value    = stringify(0),
        },{
            .driver   = "ide-drive",
            .property = "ver",
            .value    = "0.10",
        },{
            .driver   = "scsi-disk",
            .property = "ver",
            .value    = "0.10",
        },{
            .driver   = "PCI",
            .property = "rombar",
            .value    = stringify(0),
        },{
            .driver   = "PCI",
            .property = "command_serr_enable",
            .value    = "off",
        },{
            .driver   = "virtio-blk-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-serial-pci",
            .property = "event_idx",
            .value    = "off",
        },{
            .driver   = "virtio-net-pci",
            .property = "event_idx",
            .value    = "off",
        },
        { /* end of list */ }
    },
};

static QEMUMachine isapc_machine = {
    .name = "isapc",
    .desc = "ISA-only PC",
    .init = pc_init_isa,
    .max_cpus = 1,
};

#ifdef CONFIG_XEN
static QEMUMachine xenfv_machine = {
    .name = "xenfv",
    .desc = "Xen Fully-virtualized PC",
    .init = pc_xen_hvm_init,
    .max_cpus = HVM_MAX_VCPUS,
    .default_machine_opts = "accel=xen",
};
#endif

static void pc_machine_init(void)
{
    qemu_register_machine(&pc_machine);
    qemu_register_machine(&pc_machine_v0_13);
    qemu_register_machine(&pc_machine_v0_12);
    qemu_register_machine(&pc_machine_v0_11);
    qemu_register_machine(&pc_machine_v0_10);
    qemu_register_machine(&isapc_machine);
#ifdef CONFIG_XEN
    qemu_register_machine(&xenfv_machine);
#endif
}

machine_init(pc_machine_init);
