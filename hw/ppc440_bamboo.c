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
#include "boards.h"
#include "ppc440.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "device_tree.h"
#include "loader.h"
#include "elf.h"
#include "exec-memory.h"

#define BINARY_DEVICE_TREE_FILE "bamboo.dtb"

/* from u-boot */
#define KERNEL_ADDR  0x1000000
#define FDT_ADDR     0x1800000
#define RAMDISK_ADDR 0x1900000

static int bamboo_load_device_tree(target_phys_addr_t addr,
                                     uint32_t ramsize,
                                     target_phys_addr_t initrd_base,
                                     target_phys_addr_t initrd_size,
                                     const char *kernel_cmdline)
{
    int ret = -1;
#ifdef CONFIG_FDT
    uint32_t mem_reg_property[] = { 0, 0, ramsize };
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

    /* Copy data from the host device tree into the guest. Since the guest can
     * directly access the timebase without host involvement, we must expose
     * the correct frequencies. */
    if (kvm_enabled()) {
        tb_freq = kvmppc_get_tbfreq();
        clock_freq = kvmppc_get_clockfreq();
    }

    qemu_devtree_setprop_cell(fdt, "/cpus/cpu@0", "clock-frequency",
                              clock_freq);
    qemu_devtree_setprop_cell(fdt, "/cpus/cpu@0", "timebase-frequency",
                              tb_freq);

    ret = rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);
    g_free(fdt);

out:
#endif

    return ret;
}

static void bamboo_init(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model)
{
    unsigned int pci_irq_nrs[4] = { 28, 27, 26, 25 };
    MemoryRegion *address_space_mem = get_system_memory();
    PCIBus *pcibus;
    CPUState *env;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    target_phys_addr_t entry = 0;
    target_phys_addr_t loadaddr = 0;
    target_long initrd_size = 0;
    int success;
    int i;

    /* Setup CPU. */
    env = ppc440ep_init(address_space_mem, &ram_size, &pcibus,
                        pci_irq_nrs, 1, cpu_model);

    if (pcibus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            /* There are no PCI NICs on the Bamboo board, but there are
             * PCI slots, so we can pick whatever default model we want. */
            pci_nic_init_nofail(&nd_table[i], "e1000", NULL);
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

        /* Set initial guest state. */
        env->gpr[1] = (16<<20) - 8;
        env->gpr[3] = FDT_ADDR;
        env->nip = entry;
        /* XXX we currently depend on KVM to create some initial TLB entries. */
    }

    if (kvm_enabled())
        kvmppc_init();
}

static QEMUMachine bamboo_machine = {
    .name = "bamboo-0.13",
    .alias = "bamboo",
    .desc = "bamboo",
    .init = bamboo_init,
};

static QEMUMachine bamboo_machine_v0_12 = {
    .name = "bamboo-0.12",
    .desc = "bamboo",
    .init = bamboo_init,
    .compat_props = (GlobalProperty[]) {
        {
            .driver   = "virtio-serial-pci",
            .property = "max_ports",
            .value    = stringify(1),
        },{
            .driver   = "virtio-serial-pci",
            .property = "vectors",
            .value    = stringify(0),
        },
        { /* end of list */ }
    },
};

static void bamboo_machine_init(void)
{
    qemu_register_machine(&bamboo_machine);
    qemu_register_machine(&bamboo_machine_v0_12);
}

machine_init(bamboo_machine_init);
