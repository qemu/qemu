/*
 * QEMU PowerPC 440 Bamboo board emulation
 *
 * Copyright 2007 IBM Corporation.
 * Authors:
 *	Jerone Young <jyoung5@us.ibm.com>
 *	Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *	Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "config.h"
#include "qemu-common.h"
#include "net/net.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/char/serial.h"
#include "hw/ppc/ppc.h"
#include "ppc405.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"

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

#define PPC440EP_SDRAM_NR_BANKS 4

static const unsigned int ppc440ep_sdram_bank_sizes[] = {
    256<<20, 128<<20, 64<<20, 32<<20, 16<<20, 8<<20, 0
};

static hwaddr entry;

static int bamboo_load_device_tree(hwaddr addr,
                                     uint32_t ramsize,
                                     hwaddr initrd_base,
                                     hwaddr initrd_size,
                                     const char *kernel_cmdline)
{
    int ret = -1;
    uint32_t mem_reg_property[] = { 0, 0, cpu_to_be32(ramsize) };
    char *filename;
    int fdt_size;
    void *fdt;
    uint32_t tb_freq = 400000000;
    uint32_t clock_freq = 400000000;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
    if (!filename) {
        goto out;
    }
    fdt = load_device_tree(filename, &fdt_size);
    g_free(filename);
    if (fdt == NULL) {
        goto out;
    }

    /* Manipulate device tree in memory. */

    ret = qemu_fdt_setprop(fdt, "/memory", "reg", mem_reg_property,
                           sizeof(mem_reg_property));
    if (ret < 0)
        fprintf(stderr, "couldn't set /memory/reg\n");

    ret = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                initrd_base);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");

    ret = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                (initrd_base + initrd_size));
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");

    ret = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                  kernel_cmdline);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");

    /* Copy data from the host device tree into the guest. Since the guest can
     * directly access the timebase without host involvement, we must expose
     * the correct frequencies. */
    if (kvm_enabled()) {
        tb_freq = kvmppc_get_tbfreq();
        clock_freq = kvmppc_get_clockfreq();
    }

    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "clock-frequency",
                          clock_freq);
    qemu_fdt_setprop_cell(fdt, "/cpus/cpu@0", "timebase-frequency",
                          tb_freq);

    rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);
    g_free(fdt);
    return 0;

out:

    return ret;
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
    env->gpr[1] = (16<<20) - 8;
    env->gpr[3] = FDT_ADDR;
    env->nip = entry;

    /* Create a mapping for the kernel.  */
    mmubooke_create_initial_mapping(env, 0, 0);
}

static void bamboo_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    unsigned int pci_irq_nrs[4] = { 28, 27, 26, 25 };
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *isa = g_new(MemoryRegion, 1);
    MemoryRegion *ram_memories
        = g_malloc(PPC440EP_SDRAM_NR_BANKS * sizeof(*ram_memories));
    hwaddr ram_bases[PPC440EP_SDRAM_NR_BANKS];
    hwaddr ram_sizes[PPC440EP_SDRAM_NR_BANKS];
    qemu_irq *pic;
    qemu_irq *irqs;
    PCIBus *pcibus;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    hwaddr loadaddr = 0;
    target_long initrd_size = 0;
    DeviceState *dev;
    int success;
    int i;

    /* Setup CPU. */
    if (cpu_model == NULL) {
        cpu_model = "440EP";
    }
    cpu = cpu_ppc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }
    env = &cpu->env;

    qemu_register_reset(main_cpu_reset, cpu);
    ppc_booke_timers_init(cpu, 400000000, 0);
    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);

    /* SDRAM controller */
    memset(ram_bases, 0, sizeof(ram_bases));
    memset(ram_sizes, 0, sizeof(ram_sizes));
    ram_size = ppc4xx_sdram_adjust(ram_size, PPC440EP_SDRAM_NR_BANKS,
                                   ram_memories,
                                   ram_bases, ram_sizes,
                                   ppc440ep_sdram_bank_sizes);
    /* XXX 440EP's ECC interrupts are on UIC1, but we've only created UIC0. */
    ppc4xx_sdram_init(env, pic[14], PPC440EP_SDRAM_NR_BANKS, ram_memories,
                      ram_bases, ram_sizes, 1);

    /* PCI */
    dev = sysbus_create_varargs(TYPE_PPC4xx_PCI_HOST_BRIDGE,
                                PPC440EP_PCI_CONFIG,
                                pic[pci_irq_nrs[0]], pic[pci_irq_nrs[1]],
                                pic[pci_irq_nrs[2]], pic[pci_irq_nrs[3]],
                                NULL);
    pcibus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pcibus) {
        fprintf(stderr, "couldn't create PCI controller!\n");
        exit(1);
    }

    memory_region_init_alias(isa, NULL, "isa_mmio",
                             get_system_io(), 0, PPC440EP_PCI_IOLEN);
    memory_region_add_subregion(get_system_memory(), PPC440EP_PCI_IO, isa);

    if (serial_hds[0] != NULL) {
        serial_mm_init(address_space_mem, 0xef600300, 0, pic[0],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[0],
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hds[1] != NULL) {
        serial_mm_init(address_space_mem, 0xef600400, 0, pic[1],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[1],
                       DEVICE_BIG_ENDIAN);
    }

    if (pcibus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            /* There are no PCI NICs on the Bamboo board, but there are
             * PCI slots, so we can pick whatever default model we want. */
            pci_nic_init_nofail(&nd_table[i], pcibus, "e1000", NULL);
        }
    }

    /* Load kernel. */
    if (kernel_filename) {
        success = load_uimage(kernel_filename, &entry, &loadaddr, NULL);
        if (success < 0) {
            success = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                               &elf_lowaddr, NULL, 1, ELF_MACHINE, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (success < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (initrd_filename) {
        initrd_size = load_image_targphys(initrd_filename, RAMDISK_ADDR,
                                          ram_size - RAMDISK_ADDR);

        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load ram disk '%s' at %x\n",
                    initrd_filename, RAMDISK_ADDR);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (kernel_filename) {
        if (bamboo_load_device_tree(FDT_ADDR, ram_size, RAMDISK_ADDR,
                                    initrd_size, kernel_cmdline) < 0) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }
    }

    if (kvm_enabled())
        kvmppc_init();
}

static QEMUMachine bamboo_machine = {
    .name = "bamboo",
    .desc = "bamboo",
    .init = bamboo_init,
};

static void bamboo_machine_init(void)
{
    qemu_register_machine(&bamboo_machine);
}

machine_init(bamboo_machine_init);
