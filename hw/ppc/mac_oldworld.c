
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
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "mac.h"
#include "hw/input/adb.h"
#include "hw/timer/m48t59.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"

#define MAX_IDE_BUS 2
#define CFG_ADDR 0xf0000510
#define TBFREQ 16600000UL
#define CLOCKFREQ 266000000UL
#define BUSFREQ 66000000UL

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return (addr & 0x0fffffff) + KERNEL_LOAD_ADDR;
}

static hwaddr round_page(hwaddr addr)
{
    return (addr + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
}

static void ppc_heathrow_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppc_heathrow_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *boot_device = machine->boot_order;
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    char *filename;
    qemu_irq *pic, **heathrow_irqs;
    int linux_boot, i;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    uint32_t kernel_base, initrd_base, cmdline_base = 0;
    int32_t kernel_size, initrd_size;
    PCIBus *pci_bus;
    PCIDevice *macio;
    MACIOIDEState *macio_ide;
    DeviceState *dev;
    BusState *adb_bus;
    int bios_size;
    MemoryRegion *pic_mem;
    MemoryRegion *escc_mem, *escc_bar = g_new(MemoryRegion, 1);
    uint16_t ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    void *fw_cfg;
    uint64_t tbfreq;

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (machine->cpu_model == NULL)
        machine->cpu_model = "G3";
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(machine->cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        /* Set time-base frequency to 16.6 Mhz */
        cpu_ppc_tb_init(env,  TBFREQ);
        qemu_register_reset(ppc_heathrow_reset, cpu);
    }

    /* allocate RAM */
    if (ram_size > (2047 << 20)) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d MB, maximum 2047 MB\n",
                ((unsigned int)ram_size / (1 << 20)));
        exit(1);
    }

    memory_region_allocate_system_memory(ram, NULL, "ppc_heathrow.ram",
                                         ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    /* allocate and load BIOS */
    memory_region_init_ram(bios, NULL, "ppc_heathrow.bios", BIOS_SIZE,
                           &error_fatal);
    vmstate_register_ram_global(bios);

    if (bios_name == NULL)
        bios_name = PROM_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    memory_region_set_readonly(bios, true);
    memory_region_add_subregion(sysmem, PROM_ADDR, bios);

    /* Load OpenBIOS (ELF) */
    if (filename) {
        bios_size = load_elf(filename, 0, NULL, NULL, NULL, NULL,
                             1, PPC_ELF_MACHINE, 0, 0);
        g_free(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size < 0 || bios_size > BIOS_SIZE) {
        error_report("could not load PowerPC bios '%s'", bios_name);
        exit(1);
    }

    if (linux_boot) {
        uint64_t lowaddr = 0;
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_base = KERNEL_LOAD_ADDR;
        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, &lowaddr, NULL, 1, PPC_ELF_MACHINE,
                               0, 0);
        if (kernel_size < 0)
            kernel_size = load_aout(kernel_filename, kernel_base,
                                    ram_size - kernel_base, bswap_needed,
                                    TARGET_PAGE_SIZE);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              kernel_base,
                                              ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = round_page(kernel_base + kernel_size + KERNEL_GAP);
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
            cmdline_base = round_page(initrd_base + initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
            cmdline_base = round_page(kernel_base + kernel_size + KERNEL_GAP);
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
            fprintf(stderr, "No valid boot device for G3 Beige machine\n");
            exit(1);
        }
    }

    /* Register 2 MB of ISA IO space */
    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, 0x00200000);
    memory_region_add_subregion(sysmem, 0xfe000000, isa);

    /* XXX: we register only 1 output pin for heathrow PIC */
    heathrow_irqs = g_malloc0(smp_cpus * sizeof(qemu_irq *));
    heathrow_irqs[0] =
        g_malloc0(smp_cpus * sizeof(qemu_irq) * 1);
    /* Connect the heathrow PIC outputs to the 6xx bus */
    for (i = 0; i < smp_cpus; i++) {
        switch (PPC_INPUT(env)) {
        case PPC_FLAGS_INPUT_6xx:
            heathrow_irqs[i] = heathrow_irqs[0] + (i * 1);
            heathrow_irqs[i][0] =
                ((qemu_irq *)env->irq_inputs)[PPC6xx_INPUT_INT];
            break;
        default:
            error_report("Bus model not supported on OldWorld Mac machine");
            exit(1);
        }
    }

    /* Timebase Frequency */
    if (kvm_enabled()) {
        tbfreq = kvmppc_get_tbfreq();
    } else {
        tbfreq = TBFREQ;
    }

    /* init basic PC hardware */
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Only 6xx bus is supported on heathrow machine");
        exit(1);
    }
    pic = heathrow_pic_init(&pic_mem, 1, heathrow_irqs);
    pci_bus = pci_grackle_init(0xfec00000, pic,
                               get_system_memory(),
                               get_system_io());
    pci_vga_init(pci_bus);

    escc_mem = escc_init(0, pic[0x0f], pic[0x10], serial_hds[0],
                               serial_hds[1], ESCC_CLOCK, 4);
    memory_region_init_alias(escc_bar, NULL, "escc-bar",
                             escc_mem, 0, memory_region_size(escc_mem));

    for(i = 0; i < nb_nics; i++)
        pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);


    ide_drive_get(hd, ARRAY_SIZE(hd));

    macio = pci_create(pci_bus, -1, TYPE_OLDWORLD_MACIO);
    dev = DEVICE(macio);
    qdev_connect_gpio_out(dev, 0, pic[0x12]); /* CUDA */
    qdev_connect_gpio_out(dev, 1, pic[0x0D]); /* IDE-0 */
    qdev_connect_gpio_out(dev, 2, pic[0x02]); /* IDE-0 DMA */
    qdev_connect_gpio_out(dev, 3, pic[0x0E]); /* IDE-1 */
    qdev_connect_gpio_out(dev, 4, pic[0x03]); /* IDE-1 DMA */
    qdev_prop_set_uint64(dev, "frequency", tbfreq);
    macio_init(macio, pic_mem, escc_bar);

    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[0]"));
    macio_ide_init_drives(macio_ide, hd);

    macio_ide = MACIO_IDE(object_resolve_path_component(OBJECT(macio),
                                                        "ide[1]"));
    macio_ide_init_drives(macio_ide, &hd[MAX_IDE_DEVS]);

    dev = DEVICE(object_resolve_path_component(OBJECT(macio), "cuda"));
    adb_bus = qdev_get_child_bus(dev, "adb.0");
    dev = qdev_create(adb_bus, TYPE_ADB_KEYBOARD);
    qdev_init_nofail(dev);
    dev = qdev_create(adb_bus, TYPE_ADB_MOUSE);
    qdev_init_nofail(dev);

    if (usb_enabled()) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
    }

    if (graphic_depth != 15 && graphic_depth != 32 && graphic_depth != 8)
        graphic_depth = 15;

    /* No PCI init: the BIOS will do it */

    fw_cfg = fw_cfg_init_mem(CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, ARCH_HEATHROW);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_base);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, cmdline_base);
        pstrcpy_targphys("cmdline", cmdline_base, TARGET_PAGE_SIZE, kernel_cmdline);
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
#ifdef CONFIG_KVM
        uint8_t *hypercall;

        hypercall = g_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
#endif
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, tbfreq);
    /* Mac OS X requires a "known good" clock-frequency value; pass it one. */
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_CLOCKFREQ, CLOCKFREQ);
    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_BUSFREQ, BUSFREQ);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

static int heathrow_kvm_type(const char *arg)
{
    /* Always force PR KVM */
    return 2;
}

static void heathrow_machine_init(MachineClass *mc)
{
    mc->desc = "Heathrow based PowerMAC";
    mc->init = ppc_heathrow_init;
    mc->max_cpus = MAX_CPUS;
#ifndef TARGET_PPC64
    mc->is_default = 1;
#endif
    /* TOFIX "cad" when Mac floppy is implemented */
    mc->default_boot_order = "cd";
    mc->kvm_type = heathrow_kvm_type;
}

DEFINE_MACHINE("g3beige", heathrow_machine_init)
