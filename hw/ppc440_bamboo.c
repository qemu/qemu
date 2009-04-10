/*
 * Qemu PowerPC 440 Bamboo board emulation
 *
 * Copyright 2007 IBM Corporation.
 * Authors:
 * 	Jerone Young <jyoung5@us.ibm.com>
 * 	Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 * 	Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "config.h"
#include "qemu-common.h"
#include "net.h"
#include "hw.h"
#include "pci.h"
#include "virtio-blk.h"
#include "virtio-console.h"
#include "boards.h"
#include "sysemu.h"
#include "ppc440.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "device_tree.h"

#define BINARY_DEVICE_TREE_FILE "bamboo.dtb"

static void *bamboo_load_device_tree(target_phys_addr_t addr,
                                     uint32_t ramsize,
                                     target_phys_addr_t initrd_base,
                                     target_phys_addr_t initrd_size,
                                     const char *kernel_cmdline)
{
    void *fdt = NULL;
#ifdef HAVE_FDT
    uint32_t mem_reg_property[] = { 0, 0, ramsize };
    char *path;
    int fdt_size;
    int pathlen;
    int ret;

    pathlen = snprintf(NULL, 0, "%s/%s", bios_dir, BINARY_DEVICE_TREE_FILE) + 1;
    path = qemu_malloc(pathlen);

    snprintf(path, pathlen, "%s/%s", bios_dir, BINARY_DEVICE_TREE_FILE);

    fdt = load_device_tree(path, &fdt_size);
    free(path);
    if (fdt == NULL)
        goto out;

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

    if (kvm_enabled())
        kvmppc_fdt_update(fdt);

    cpu_physical_memory_write (addr, (void *)fdt, fdt_size);

out:
#endif

    return fdt;
}

static void bamboo_init(ram_addr_t ram_size, int vga_ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    unsigned int pci_irq_nrs[4] = { 28, 27, 26, 25 };
    PCIBus *pcibus;
    CPUState *env;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    target_ulong entry = 0;
    target_ulong loadaddr = 0;
    target_long kernel_size = 0;
    target_ulong initrd_base = 0;
    target_long initrd_size = 0;
    target_ulong dt_base = 0;
    void *fdt;
    int i;

    /* Setup CPU. */
    env = ppc440ep_init(&ram_size, &pcibus, pci_irq_nrs, 1);

    if (pcibus) {
        int unit_id = 0;

        /* Add virtio block devices. */
        while ((i = drive_get_index(IF_VIRTIO, 0, unit_id)) != -1) {
            virtio_blk_init(pcibus, drives_table[i].bdrv);
            unit_id++;
        }

        /* Add virtio console devices */
        for(i = 0; i < MAX_VIRTIO_CONSOLES; i++) {
            if (virtcon_hds[i])
                virtio_console_init(pcibus, virtcon_hds[i]);
        }

        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            /* There are no PCI NICs on the Bamboo board, but there are
             * PCI slots, so we can pick whatever default model we want. */
            pci_nic_init(pcibus, &nd_table[i], -1, "e1000");
        }
    }

    /* Load kernel. */
    if (kernel_filename) {
        kernel_size = load_uimage(kernel_filename, &entry, &loadaddr, NULL);
        if (kernel_size < 0) {
            kernel_size = load_elf(kernel_filename, 0, &elf_entry, &elf_lowaddr,
                                   NULL);
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
        initrd_base = kernel_size + loadaddr;
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
        if (initrd_base)
            dt_base = initrd_base + initrd_size;
        else
            dt_base = kernel_size + loadaddr;

        fdt = bamboo_load_device_tree(dt_base, ram_size,
                                      initrd_base, initrd_size, kernel_cmdline);
        if (fdt == NULL) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }

        /* Set initial guest state. */
        env->gpr[1] = (16<<20) - 8;
        env->gpr[3] = dt_base;
        env->nip = entry;
        /* XXX we currently depend on KVM to create some initial TLB entries. */
    }

    if (kvm_enabled())
        kvmppc_init();
}

QEMUMachine bamboo_machine = {
    .name = "bamboo",
    .desc = "bamboo",
    .init = bamboo_init,
    .ram_require = 8<<20 | RAMSIZE_FIXED,
};
