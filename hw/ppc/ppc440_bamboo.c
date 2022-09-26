/*
 * QEMU PowerPC 440 Bamboo board emulation
 *
 * Copyright 2007 IBM Corporation.
 * Authors:
 *  Jerone Young <jyoung5@us.ibm.com>
 *  Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *  Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "net/net.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/char/serial.h"
#include "hw/ppc/ppc.h"
#include "ppc405.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "hw/intc/ppc-uic.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#include <libfdt.h>

#define BINARY_DEVICE_TREE_FILE "bamboo.dtb"

/* from u-boot */
#define KERNEL_ADDR  0x1000000
#define FDT_ADDR     0x1800000
#define RAMDISK_ADDR 0x1900000

#define PPC440EP_PCI_CONFIG     0xeec00000
#define PPC440EP_PCI_INTACK     0xeed00000
#define PPC440EP_PCI_SPECIAL    0xeed00000
#define PPC440EP_PCI_REGS       0xef400000
#define PPC440EP_PCI_IO         0xe8000000
#define PPC440EP_PCI_IOLEN      0x00010000

static hwaddr entry;

static int bamboo_load_device_tree(MachineState *machine,
                                   hwaddr addr,
                                   hwaddr initrd_base,
                                   hwaddr initrd_size)
{
    int ret = -1;
    uint32_t mem_reg_property[] = { 0, 0, cpu_to_be32(machine->ram_size) };
    char *filename;
    int fdt_size;
    void *fdt;
    uint32_t tb_freq = 400000000;
    uint32_t clock_freq = 400000000;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
    if (!filename) {
        return -1;
    }
    fdt = load_device_tree(filename, &fdt_size);
    g_free(filename);
    if (fdt == NULL) {
        return -1;
    }

    /* Manipulate device tree in memory. */

    ret = qemu_fdt_setprop(fdt, "/memory", "reg", mem_reg_property,
                           sizeof(mem_reg_property));
    if (ret < 0) {
        fprintf(stderr, "couldn't set /memory/reg\n");
    }
    ret = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                initrd_base);
    if (ret < 0) {
        fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
    }
    ret = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                (initrd_base + initrd_size));
    if (ret < 0) {
        fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
    }
    ret = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                  machine->kernel_cmdline);
    if (ret < 0) {
        fprintf(stderr, "couldn't set /chosen/bootargs\n");
    }

    /*
     * Copy data from the host device tree into the guest. Since the guest can
     * directly access the timebase without host involvement, we must expose
     * the correct frequencies.
     */
    if (kvm_enabled()) {
        tb_freq = kvmppc_get_tbfreq();
        clock_freq = kvmppc_get_clockfreq();
    }

    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "clock-frequency",
                          clock_freq);
    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "timebase-frequency",
                          tb_freq);

    rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);

    /* Set ms->fdt for 'dumpdtb' QMP/HMP command */
    machine->fdt = fdt;

    return 0;
}

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0x80000000  */
    tlb->EPN = va & TARGET_PAGE_MASK;
    tlb->RPN = pa & TARGET_PAGE_MASK;
    tlb->PID = 0;

    tlb = &env->tlb.tlbe[1];
    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0xffffffff  */
    tlb->EPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    env->gpr[1] = (16 * MiB) - 8;
    env->gpr[3] = FDT_ADDR;
    env->nip = entry;

    /* Create a mapping for the kernel.  */
    mmubooke_create_initial_mapping(env, 0, 0);
}

static void bamboo_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    unsigned int pci_irq_nrs[4] = { 28, 27, 26, 25 };
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    PCIBus *pcibus;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    target_long initrd_size = 0;
    DeviceState *dev;
    DeviceState *uicdev;
    SysBusDevice *uicsbd;
    int success;
    int i;

    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    if (env->mmu_model != POWERPC_MMU_BOOKE) {
        error_report("MMU model %i not supported by this machine",
                     env->mmu_model);
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, cpu);
    ppc_booke_timers_init(cpu, 400000000, 0);
    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    uicdev = qdev_new(TYPE_PPC_UIC);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(uicdev), cpu, &error_fatal);
    object_unref(OBJECT(uicdev));
    uicsbd = SYS_BUS_DEVICE(uicdev);
    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_INT,
                       qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_INT));
    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_CINT,
                       qdev_get_gpio_in(DEVICE(cpu), PPC40x_INPUT_CINT));

    /* SDRAM controller */
    dev = qdev_new(TYPE_PPC4xx_SDRAM_DDR);
    object_property_set_link(OBJECT(dev), "dram", OBJECT(machine->ram),
                             &error_abort);
    ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(dev), cpu, &error_fatal);
    object_unref(OBJECT(dev));
    /* XXX 440EP's ECC interrupts are on UIC1, but we've only created UIC0. */
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(uicdev, 14));
    /* Enable SDRAM memory regions, this should be done by the firmware */
    ppc4xx_sdram_ddr_enable(PPC4xx_SDRAM_DDR(dev));

    /* PCI */
    dev = sysbus_create_varargs(TYPE_PPC4xx_PCI_HOST_BRIDGE,
                                PPC440EP_PCI_CONFIG,
                                qdev_get_gpio_in(uicdev, pci_irq_nrs[0]),
                                qdev_get_gpio_in(uicdev, pci_irq_nrs[1]),
                                qdev_get_gpio_in(uicdev, pci_irq_nrs[2]),
                                qdev_get_gpio_in(uicdev, pci_irq_nrs[3]),
                                NULL);
    pcibus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pcibus) {
        error_report("couldn't create PCI controller");
        exit(1);
    }

    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, PPC440EP_PCI_IOLEN);
    memory_region_add_subregion(get_system_memory(), PPC440EP_PCI_IO, isa);

    if (serial_hd(0) != NULL) {
        serial_mm_init(address_space_mem, 0xef600300, 0,
                       qdev_get_gpio_in(uicdev, 0),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(0),
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hd(1) != NULL) {
        serial_mm_init(address_space_mem, 0xef600400, 0,
                       qdev_get_gpio_in(uicdev, 1),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(1),
                       DEVICE_BIG_ENDIAN);
    }

    if (pcibus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            /*
             * There are no PCI NICs on the Bamboo board, but there are
             * PCI slots, so we can pick whatever default model we want.
             */
            pci_nic_init_nofail(&nd_table[i], pcibus, "e1000", NULL);
        }
    }

    /* Load kernel. */
    if (kernel_filename) {
        hwaddr loadaddr = LOAD_UIMAGE_LOADADDR_INVALID;
        success = load_uimage(kernel_filename, &entry, &loadaddr, NULL,
                              NULL, NULL);
        if (success < 0) {
            uint64_t elf_entry;
            success = load_elf(kernel_filename, NULL, NULL, NULL, &elf_entry,
                               NULL, NULL, NULL, 1, PPC_ELF_MACHINE, 0, 0);
            entry = elf_entry;
        }
        /* XXX try again as binary */
        if (success < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (initrd_filename) {
        initrd_size = load_image_targphys(initrd_filename, RAMDISK_ADDR,
                                          machine->ram_size - RAMDISK_ADDR);

        if (initrd_size < 0) {
            error_report("could not load ram disk '%s' at %x",
                         initrd_filename, RAMDISK_ADDR);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (kernel_filename) {
        if (bamboo_load_device_tree(machine, FDT_ADDR,
                                    RAMDISK_ADDR, initrd_size) < 0) {
            error_report("couldn't load device tree");
            exit(1);
        }
    }
}

static void bamboo_machine_init(MachineClass *mc)
{
    mc->desc = "bamboo";
    mc->init = bamboo_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("440epb");
    mc->default_ram_id = "ppc4xx.sdram";
}

DEFINE_MACHINE("bamboo", bamboo_machine_init)
