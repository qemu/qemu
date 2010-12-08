/*
 * Qemu PowerPC MPC8544DS board emualtion
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This file is derived from hw/ppc440_bamboo.c,
 * the copyright for that material belongs to the original owners.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include <dirent.h>

#include "config.h"
#include "qemu-common.h"
#include "net.h"
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "boards.h"
#include "sysemu.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "device_tree.h"
#include "openpic.h"
#include "ppce500.h"
#include "loader.h"
#include "elf.h"

#define BINARY_DEVICE_TREE_FILE    "mpc8544ds.dtb"
#define UIMAGE_LOAD_BASE           0
#define DTC_LOAD_PAD               0x500000
#define DTC_PAD_MASK               0xFFFFF
#define INITRD_LOAD_PAD            0x2000000
#define INITRD_PAD_MASK            0xFFFFFF

#define RAM_SIZES_ALIGN            (64UL << 20)

#define MPC8544_CCSRBAR_BASE       0xE0000000
#define MPC8544_MPIC_REGS_BASE     (MPC8544_CCSRBAR_BASE + 0x40000)
#define MPC8544_SERIAL0_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4500)
#define MPC8544_SERIAL1_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4600)
#define MPC8544_PCI_REGS_BASE      (MPC8544_CCSRBAR_BASE + 0x8000)
#define MPC8544_PCI_REGS_SIZE      0x1000
#define MPC8544_PCI_IO             0xE1000000
#define MPC8544_PCI_IOLEN          0x10000

#ifdef CONFIG_FDT
static int mpc8544_copy_soc_cell(void *fdt, const char *node, const char *prop)
{
    uint32_t cell;
    int ret;

    ret = kvmppc_read_host_property(node, prop, &cell, sizeof(cell));
    if (ret < 0) {
        fprintf(stderr, "couldn't read host %s/%s\n", node, prop);
        goto out;
    }

    ret = qemu_devtree_setprop_cell(fdt, "/cpus/PowerPC,8544@0",
                                prop, cell);
    if (ret < 0) {
        fprintf(stderr, "couldn't set guest /cpus/PowerPC,8544@0/%s\n", prop);
        goto out;
    }

out:
    return ret;
}
#endif

static int mpc8544_load_device_tree(target_phys_addr_t addr,
                                     uint32_t ramsize,
                                     target_phys_addr_t initrd_base,
                                     target_phys_addr_t initrd_size,
                                     const char *kernel_cmdline)
{
    int ret = -1;
#ifdef CONFIG_FDT
    uint32_t mem_reg_property[] = {0, ramsize};
    char *filename;
    int fdt_size;
    void *fdt;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
    if (!filename) {
        goto out;
    }
    fdt = load_device_tree(filename, &fdt_size);
    qemu_free(filename);
    if (fdt == NULL) {
        goto out;
    }

    /* Manipulate device tree in memory. */
    ret = qemu_devtree_setprop(fdt, "/memory", "reg", mem_reg_property,
                               sizeof(mem_reg_property));
    if (ret < 0)
        fprintf(stderr, "couldn't set /memory/reg\n");

    ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                    initrd_base);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");

    ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                    (initrd_base + initrd_size));
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");

    ret = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs",
                                      kernel_cmdline);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");

    if (kvm_enabled()) {
        struct dirent *dirp;
        DIR *dp;
        char buf[128];

        if ((dp = opendir("/proc/device-tree/cpus/")) == NULL) {
            printf("Can't open directory /proc/device-tree/cpus/\n");
            ret = -1;
            goto out;
        }

        buf[0] = '\0';
        while ((dirp = readdir(dp)) != NULL) {
            if (strncmp(dirp->d_name, "PowerPC", 7) == 0) {
                snprintf(buf, 128, "/cpus/%s", dirp->d_name);
                break;
            }
        }
        closedir(dp);
        if (buf[0] == '\0') {
            printf("Unknow host!\n");
            ret = -1;
            goto out;
        }

        mpc8544_copy_soc_cell(fdt, buf, "clock-frequency");
        mpc8544_copy_soc_cell(fdt, buf, "timebase-frequency");
    }

    ret = rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);
    qemu_free(fdt);

out:
#endif

    return ret;
}

static void mpc8544ds_init(ram_addr_t ram_size,
                         const char *boot_device,
                         const char *kernel_filename,
                         const char *kernel_cmdline,
                         const char *initrd_filename,
                         const char *cpu_model)
{
    PCIBus *pci_bus;
    CPUState *env;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    target_phys_addr_t entry=0;
    target_phys_addr_t loadaddr=UIMAGE_LOAD_BASE;
    target_long kernel_size=0;
    target_ulong dt_base = 0;
    target_ulong initrd_base = 0;
    target_long initrd_size=0;
    int i=0;
    unsigned int pci_irq_nrs[4] = {1, 2, 3, 4};
    qemu_irq *irqs, *mpic, *pci_irqs;

    /* Setup CPU */
    env = cpu_ppc_init("e500v2_v30");
    if (!env) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }

    /* Fixup Memory size on a alignment boundary */
    ram_size &= ~(RAM_SIZES_ALIGN - 1);

    /* Register Memory */
    cpu_register_physical_memory(0, ram_size, qemu_ram_alloc(NULL,
                                 "mpc8544ds.ram", ram_size));

    /* MPIC */
    irqs = qemu_mallocz(sizeof(qemu_irq) * OPENPIC_OUTPUT_NB);
    irqs[OPENPIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPCE500_INPUT_INT];
    irqs[OPENPIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPCE500_INPUT_CINT];
    mpic = mpic_init(MPC8544_MPIC_REGS_BASE, 1, &irqs, NULL);

    /* Serial */
    if (serial_hds[0]) {
        serial_mm_init(MPC8544_SERIAL0_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], 1, 1);
    }

    if (serial_hds[1]) {
        serial_mm_init(MPC8544_SERIAL1_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], 1, 1);
    }

    /* PCI */
    pci_irqs = qemu_malloc(sizeof(qemu_irq) * 4);
    pci_irqs[0] = mpic[pci_irq_nrs[0]];
    pci_irqs[1] = mpic[pci_irq_nrs[1]];
    pci_irqs[2] = mpic[pci_irq_nrs[2]];
    pci_irqs[3] = mpic[pci_irq_nrs[3]];
    pci_bus = ppce500_pci_init(pci_irqs, MPC8544_PCI_REGS_BASE);
    if (!pci_bus)
        printf("couldn't create PCI controller!\n");

    isa_mmio_init(MPC8544_PCI_IO, MPC8544_PCI_IOLEN);

    if (pci_bus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], "virtio", NULL);
        }
    }

    /* Load kernel. */
    if (kernel_filename) {
        kernel_size = load_uimage(kernel_filename, &entry, &loadaddr, NULL);
        if (kernel_size < 0) {
            kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                                   &elf_lowaddr, NULL, 1, ELF_MACHINE, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (initrd_filename) {
        initrd_base = (kernel_size + INITRD_LOAD_PAD) & ~INITRD_PAD_MASK;
        initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                          ram_size - initrd_base);

        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    initrd_filename);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (kernel_filename) {
        dt_base = (kernel_size + DTC_LOAD_PAD) & ~DTC_PAD_MASK;
        if (mpc8544_load_device_tree(dt_base, ram_size,
                    initrd_base, initrd_size, kernel_cmdline) < 0) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }

        cpu_synchronize_state(env);

        /* Set initial guest state. */
        env->gpr[1] = (16<<20) - 8;
        env->gpr[3] = dt_base;
        env->nip = entry;
        /* XXX we currently depend on KVM to create some initial TLB entries. */
    }

    if (kvm_enabled())
        kvmppc_init();

    return;
}

static QEMUMachine mpc8544ds_machine = {
    .name = "mpc8544ds",
    .desc = "mpc8544ds",
    .init = mpc8544ds_init,
};

static void mpc8544ds_machine_init(void)
{
    qemu_register_machine(&mpc8544ds_machine);
}

machine_init(mpc8544ds_machine_init);
