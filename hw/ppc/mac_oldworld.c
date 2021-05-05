
/*
 * QEMU OldWorld PowerMac (currently ~G3 Beige) hardware System Emulator
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/ppc/ppc.h"
#include "hw/qdev-properties.h"
#include "mac.h"
#include "hw/input/adb.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/misc/macio/macio.h"
#include "hw/loader.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "kvm_ppc.h"

#define MAX_IDE_BUS 2
#define CFG_ADDR 0xf0000510
#define TBFREQ 16600000UL
#define CLOCKFREQ 266000000UL
#define BUSFREQ 66000000UL

#define NDRV_VGA_FILENAME "qemu_vga.ndrv"

#define GRACKLE_BASE 0xfec00000
#define PROM_BASE 0xffc00000
#define PROM_SIZE (4 * MiB)

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static void ppc_heathrow_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppc_heathrow_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *bios_name = machine->firmware ?: PROM_FILENAME;
    const char *boot_device = machine->boot_order;
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    char *filename;
    int i;
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    uint32_t kernel_base, initrd_base, cmdline_base = 0;
    int32_t kernel_size, initrd_size;
    PCIBus *pci_bus;
    PCIDevice *macio;
    MACIOIDEState *macio_ide;
    ESCCState *escc;
    SysBusDevice *s;
    DeviceState *dev, *pic_dev, *grackle_dev;
    BusState *adb_bus;
    uint64_t bios_addr;
    int bios_size;
    unsigned int smp_cpus = machine->smp.cpus;
    uint16_t ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    void *fw_cfg;
    uint64_t tbfreq;

    /* init CPUs */
    for (i = 0; i < smp_cpus; i++) {
        cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
        env = &cpu->env;

        /* Set time-base frequency to 16.6 Mhz */
        cpu_ppc_tb_init(env,  TBFREQ);
        qemu_register_reset(ppc_heathrow_reset, cpu);
    }

    /* allocate RAM */
    if (ram_size > 2047 * MiB) {
        error_report("Too much memory for this machine: %" PRId64 " MB, "
                     "maximum 2047 MB", ram_size / MiB);
        exit(1);
    }

    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /* allocate and load firmware ROM */
    memory_region_init_rom(bios, NULL, "ppc_heathrow.bios", PROM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), PROM_BASE, bios);

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        /* Load OpenBIOS (ELF) */
        bios_size = load_elf(filename, NULL, NULL, NULL, NULL, &bios_addr,
                             NULL, NULL, 1, PPC_ELF_MACHINE, 0, 0);
        /* Unfortunately, load_elf sign-extends reading elf32 */
        bios_addr = (uint32_t)bios_addr;

        if (bios_size <= 0) {
            /* or if could not load ELF try loading a binary ROM image */
            bios_size = load_image_targphys(filename, PROM_BASE, PROM_SIZE);
            bios_addr = PROM_BASE;
        }
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size < 0 || bios_addr - PROM_BASE + bios_size > PROM_SIZE) {
        error_report("could not load PowerPC bios '%s'", bios_name);
        exit(1);
    }

    if (machine->kernel_filename) {
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_base = KERNEL_LOAD_ADDR;
        kernel_size = load_elf(machine->kernel_filename, NULL,
                               translate_kernel_address, NULL, NULL, NULL,
                               NULL, NULL, 1, PPC_ELF_MACHINE, 0, 0);
        if (kernel_size < 0)
            kernel_size = load_aout(machine->kernel_filename, kernel_base,
                                    ram_size - kernel_base, bswap_needed,
                                    TARGET_PAGE_SIZE);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(machine->kernel_filename,
                                              kernel_base,
                                              ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (machine->initrd_filename) {
            initrd_base = TARGET_PAGE_ALIGN(kernel_base + kernel_size +
                                            KERNEL_GAP);
            initrd_size = load_image_targphys(machine->initrd_filename,
                                              initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             machine->initrd_filename);
                exit(1);
            }
            cmdline_base = TARGET_PAGE_ALIGN(initrd_base + initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
            cmdline_base = TARGET_PAGE_ALIGN(kernel_base + kernel_size + KERNEL_GAP);
        }
        ppc_boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
        ppc_boot_device = '\0';
        for (i = 0; boot_device[i] != '\0'; i++) {
            /* TOFIX: for now, the second IDE channel is not properly
             *        used by OHW. The Mac floppy disk are not emulated.
             *        For now, OHW cannot boot from the network.
             */
#if 0
            if (boot_device[i] >= 'a' && boot_device[i] <= 'f') {
                ppc_boot_device = boot_device[i];
                break;
            }
#else
            if (boot_device[i] >= 'c' && boot_device[i] <= 'd') {
                ppc_boot_device = boot_device[i];
                break;
            }
#endif
        }
        if (ppc_boot_device == '\0') {
            error_report("No valid boot device for G3 Beige machine");
            exit(1);
        }
    }

    /* Timebase Frequency */
    if (kvm_enabled()) {
        tbfreq = kvmppc_get_tbfreq();
    } else {
        tbfreq = TBFREQ;
    }

    /* Grackle PCI host bridge */
    grackle_dev = qdev_new(TYPE_GRACKLE_PCI_HOST_BRIDGE);
    qdev_prop_set_uint32(grackle_dev, "ofw-addr", 0x80000000);
    s = SYS_BUS_DEVICE(grackle_dev);
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, GRACKLE_BASE);
    sysbus_mmio_map(s, 1, GRACKLE_BASE + 0x200000);
    /* PCI hole */
    memory_region_add_subregion(get_system_memory(), 0x80000000ULL,
                                sysbus_mmio_get_region(s, 2));
    /* Register 2 MB of ISA IO space */
    memory_region_add_subregion(get_system_memory(), 0xfe000000,
                                sysbus_mmio_get_region(s, 3));

    pci_bus = PCI_HOST_BRIDGE(grackle_dev)->bus;

    /* MacIO */
    macio = pci_new(PCI_DEVFN(16, 0), TYPE_OLDWORLD_MACIO);
    dev = DEVICE(macio);
    qdev_prop_set_uint64(dev, "frequency", tbfreq);

    escc = ESCC(object_resolve_path_component(OBJECT(macio), "escc"));
    qdev_prop_set_chr(DEVICE(escc), "chrA", serial_hd(0));
    qdev_prop_set_chr(DEVICE(escc), "chrB", serial_hd(1));

    pci_realize_and_unref(macio, pci_bus, &error_fatal);

    pic_dev = DEVICE(object_resolve_path_component(OBJECT(macio), "pic"));
    for (i = 0; i < 4; i++) {
        qdev_connect_gpio_out(grackle_dev, i,
                              qdev_get_gpio_in(pic_dev, 0x15 + i));
    }

    /* Connect the heathrow PIC outputs to the 6xx bus */
    for (i = 0; i < smp_cpus; i++) {
        switch (PPC_INPUT(env)) {
        case PPC_FLAGS_INPUT_6xx:
            /* XXX: we register only 1 output pin for heathrow PIC */
            qdev_connect_gpio_out(pic_dev, 0,
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_INT]);
            break;
        default:
            error_report("Bus model not supported on OldWorld Mac machine");
            exit(1);
        }
    }

    pci_vga_init(pci_bus);

    for (i = 0; i < nb_nics; i++) {
        pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);
    }

    /* MacIO IDE */
    ide_drive_get(hd, ARRAY_SIZE(hd));
    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[0]"));
    macio_ide_init_drives(macio_ide, hd);

    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[1]"));
    macio_ide_init_drives(macio_ide, &hd[MAX_IDE_DEVS]);

    /* MacIO CUDA/ADB */
    dev = DEVICE(object_resolve_path_component(OBJECT(macio), "cuda"));
    adb_bus = qdev_get_child_bus(dev, "adb.0");
    dev = qdev_new(TYPE_ADB_KEYBOARD);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);
    dev = qdev_new(TYPE_ADB_MOUSE);
    qdev_realize_and_unref(dev, adb_bus, &error_fatal);

    if (machine_usb(machine)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
    }

    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8)
        graphic_depth = 15;

    /* No PCI init: the BIOS will do it */

    dev = qdev_new(TYPE_FW_CFG_MEM);
    fw_cfg = FW_CFG(dev);
    qdev_prop_set_uint32(dev, "data_width", 1);
    qdev_prop_set_bit(dev, "dma_enabled", false);
    object_property_add_child(OBJECT(qdev_get_machine()), TYPE_FW_CFG,
                              OBJECT(fw_cfg));
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, CFG_ADDR);
    sysbus_mmio_map(s, 1, CFG_ADDR + 2);

    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)machine->smp.max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, ARCH_HEATHROW);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_base);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (machine->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, cmdline_base);
        pstrcpy_targphys("cmdline", cmdline_base, TARGET_PAGE_SIZE,
                         machine->kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_base);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, ppc_boot_device);

    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_DEPTH, graphic_depth);

    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_IS_KVM, kvm_enabled());
    if (kvm_enabled()) {
        uint8_t *hypercall;

        hypercall = g_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, tbfreq);
    /* Mac OS X requires a "known good" clock-frequency value; pass it one. */
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_CLOCKFREQ, CLOCKFREQ);
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_BUSFREQ, BUSFREQ);

    /* MacOS NDRV VGA driver */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, NDRV_VGA_FILENAME);
    if (filename) {
        gchar *ndrv_file;
        gsize ndrv_size;

        if (g_file_get_contents(filename, &ndrv_file, &ndrv_size, NULL)) {
            fw_cfg_add_file(fw_cfg, "ndrv/qemu_vga.ndrv", ndrv_file, ndrv_size);
        }
        g_free(filename);
    }

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

/*
 * Implementation of an interface to adjust firmware path
 * for the bootindex property handling.
 */
static char *heathrow_fw_dev_path(FWPathProvider *p, BusState *bus,
                                  DeviceState *dev)
{
    PCIDevice *pci;
    MACIOIDEState *macio_ide;

    if (!strcmp(object_get_typename(OBJECT(dev)), "macio-oldworld")) {
        pci = PCI_DEVICE(dev);
        return g_strdup_printf("mac-io@%x", PCI_SLOT(pci->devfn));
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "macio-ide")) {
        macio_ide = MACIO_IDE(dev);
        return g_strdup_printf("ata-3@%x", macio_ide->addr);
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "ide-hd")) {
        return g_strdup("disk");
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "ide-cd")) {
        return g_strdup("cdrom");
    }

    if (!strcmp(object_get_typename(OBJECT(dev)), "virtio-blk-device")) {
        return g_strdup("disk");
    }

    return NULL;
}

static int heathrow_kvm_type(MachineState *machine, const char *arg)
{
    /* Always force PR KVM */
    return 2;
}

static void heathrow_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(oc);

    mc->desc = "Heathrow based PowerMAC";
    mc->init = ppc_heathrow_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = MAX_CPUS;
#ifndef TARGET_PPC64
    mc->is_default = true;
#endif
    /* TOFIX "cad" when Mac floppy is implemented */
    mc->default_boot_order = "cd";
    mc->kvm_type = heathrow_kvm_type;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("750_v3.1");
    mc->default_display = "std";
    mc->ignore_boot_device_suffixes = true;
    mc->default_ram_id = "ppc_heathrow.ram";
    fwc->get_dev_path = heathrow_fw_dev_path;
}

static const TypeInfo ppc_heathrow_machine_info = {
    .name          = MACHINE_TYPE_NAME("g3beige"),
    .parent        = TYPE_MACHINE,
    .class_init    = heathrow_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { }
    },
};

static void ppc_heathrow_register_types(void)
{
    type_register_static(&ppc_heathrow_machine_info);
}

type_init(ppc_heathrow_register_types);
