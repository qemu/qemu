/*
 * QEMU PowerPC e500-based platforms
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

#include "config.h"
#include "qemu-common.h"
#include "e500.h"
#include "net.h"
#include "hw/hw.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "hw/boards.h"
#include "sysemu.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "device_tree.h"
#include "hw/openpic.h"
#include "hw/ppc.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "exec-memory.h"
#include "host-utils.h"

#define BINARY_DEVICE_TREE_FILE    "mpc8544ds.dtb"
#define UIMAGE_LOAD_BASE           0
#define DTC_LOAD_PAD               0x500000
#define DTC_PAD_MASK               0xFFFFF
#define INITRD_LOAD_PAD            0x2000000
#define INITRD_PAD_MASK            0xFFFFFF

#define RAM_SIZES_ALIGN            (64UL << 20)

/* TODO: parameterize */
#define MPC8544_CCSRBAR_BASE       0xE0000000ULL
#define MPC8544_CCSRBAR_SIZE       0x00100000ULL
#define MPC8544_MPIC_REGS_BASE     (MPC8544_CCSRBAR_BASE + 0x40000ULL)
#define MPC8544_SERIAL0_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4500ULL)
#define MPC8544_SERIAL1_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4600ULL)
#define MPC8544_PCI_REGS_BASE      (MPC8544_CCSRBAR_BASE + 0x8000ULL)
#define MPC8544_PCI_REGS_SIZE      0x1000ULL
#define MPC8544_PCI_IO             0xE1000000ULL
#define MPC8544_PCI_IOLEN          0x10000ULL
#define MPC8544_UTIL_BASE          (MPC8544_CCSRBAR_BASE + 0xe0000ULL)
#define MPC8544_SPIN_BASE          0xEF000000ULL

struct boot_info
{
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};

static void pci_map_create(void *fdt, uint32_t *pci_map, uint32_t mpic)
{
    int i;
    const uint32_t tmp[] = {
                             /* IDSEL 0x11 J17 Slot 1 */
                             0x8800, 0x0, 0x0, 0x1, mpic, 0x2, 0x1,
                             0x8800, 0x0, 0x0, 0x2, mpic, 0x3, 0x1,
                             0x8800, 0x0, 0x0, 0x3, mpic, 0x4, 0x1,
                             0x8800, 0x0, 0x0, 0x4, mpic, 0x1, 0x1,

                             /* IDSEL 0x12 J16 Slot 2 */
                             0x9000, 0x0, 0x0, 0x1, mpic, 0x3, 0x1,
                             0x9000, 0x0, 0x0, 0x2, mpic, 0x4, 0x1,
                             0x9000, 0x0, 0x0, 0x3, mpic, 0x2, 0x1,
                             0x9000, 0x0, 0x0, 0x4, mpic, 0x1, 0x1,
                           };
    for (i = 0; i < (7 * 8); i++) {
        pci_map[i] = cpu_to_be32(tmp[i]);
    }
}

static void dt_serial_create(void *fdt, unsigned long long offset,
                             const char *soc, const char *mpic,
                             const char *alias, int idx, bool defcon)
{
    char ser[128];

    snprintf(ser, sizeof(ser), "%s/serial@%llx", soc, offset);
    qemu_devtree_add_subnode(fdt, ser);
    qemu_devtree_setprop_string(fdt, ser, "device_type", "serial");
    qemu_devtree_setprop_string(fdt, ser, "compatible", "ns16550");
    qemu_devtree_setprop_cells(fdt, ser, "reg", offset, 0x100);
    qemu_devtree_setprop_cell(fdt, ser, "cell-index", idx);
    qemu_devtree_setprop_cell(fdt, ser, "clock-frequency", 0);
    qemu_devtree_setprop_cells(fdt, ser, "interrupts", 42, 2);
    qemu_devtree_setprop_phandle(fdt, ser, "interrupt-parent", mpic);
    qemu_devtree_setprop_string(fdt, "/aliases", alias, ser);

    if (defcon) {
        qemu_devtree_setprop_string(fdt, "/chosen", "linux,stdout-path", ser);
    }
}

static int ppce500_load_device_tree(CPUPPCState *env,
                                    PPCE500Params *params,
                                    target_phys_addr_t addr,
                                    target_phys_addr_t initrd_base,
                                    target_phys_addr_t initrd_size)
{
    int ret = -1;
    uint64_t mem_reg_property[] = { 0, cpu_to_be64(params->ram_size) };
    int fdt_size;
    void *fdt;
    uint8_t hypercall[16];
    uint32_t clock_freq = 400000000;
    uint32_t tb_freq = 400000000;
    int i;
    const char *toplevel_compat = NULL; /* user override */
    char compatible_sb[] = "fsl,mpc8544-immr\0simple-bus";
    char soc[128];
    char mpic[128];
    uint32_t mpic_ph;
    char gutil[128];
    char pci[128];
    uint32_t pci_map[7 * 8];
    uint32_t pci_ranges[14] =
        {
            0x2000000, 0x0, 0xc0000000,
            0x0, 0xc0000000,
            0x0, 0x20000000,

            0x1000000, 0x0, 0x0,
            0x0, 0xe1000000,
            0x0, 0x10000,
        };
    QemuOpts *machine_opts;
    const char *dumpdtb = NULL;
    const char *dtb_file = NULL;

    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts) {
        dumpdtb = qemu_opt_get(machine_opts, "dumpdtb");
        dtb_file = qemu_opt_get(machine_opts, "dtb");
        toplevel_compat = qemu_opt_get(machine_opts, "dt_compatible");
    }

    if (dtb_file) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, dtb_file);
        if (!filename) {
            goto out;
        }

        fdt = load_device_tree(filename, &fdt_size);
        if (!fdt) {
            goto out;
        }
        goto done;
    }

    fdt = create_device_tree(&fdt_size);
    if (fdt == NULL) {
        goto out;
    }

    /* Manipulate device tree in memory. */
    qemu_devtree_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_devtree_setprop_cell(fdt, "/", "#size-cells", 2);

    qemu_devtree_add_subnode(fdt, "/memory");
    qemu_devtree_setprop_string(fdt, "/memory", "device_type", "memory");
    qemu_devtree_setprop(fdt, "/memory", "reg", mem_reg_property,
                         sizeof(mem_reg_property));

    qemu_devtree_add_subnode(fdt, "/chosen");
    if (initrd_size) {
        ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                        initrd_base);
        if (ret < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
        }

        ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                        (initrd_base + initrd_size));
        if (ret < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
        }
    }

    ret = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs",
                                      params->kernel_cmdline);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");

    if (kvm_enabled()) {
        /* Read out host's frequencies */
        clock_freq = kvmppc_get_clockfreq();
        tb_freq = kvmppc_get_tbfreq();

        /* indicate KVM hypercall interface */
        qemu_devtree_add_subnode(fdt, "/hypervisor");
        qemu_devtree_setprop_string(fdt, "/hypervisor", "compatible",
                                    "linux,kvm");
        kvmppc_get_hypercall(env, hypercall, sizeof(hypercall));
        qemu_devtree_setprop(fdt, "/hypervisor", "hcall-instructions",
                             hypercall, sizeof(hypercall));
    }

    /* Create CPU nodes */
    qemu_devtree_add_subnode(fdt, "/cpus");
    qemu_devtree_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_devtree_setprop_cell(fdt, "/cpus", "#size-cells", 0);

    /* We need to generate the cpu nodes in reverse order, so Linux can pick
       the first node as boot node and be happy */
    for (i = smp_cpus - 1; i >= 0; i--) {
        char cpu_name[128];
        uint64_t cpu_release_addr = MPC8544_SPIN_BASE + (i * 0x20);

        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (env->cpu_index == i) {
                break;
            }
        }

        if (!env) {
            continue;
        }

        snprintf(cpu_name, sizeof(cpu_name), "/cpus/PowerPC,8544@%x", env->cpu_index);
        qemu_devtree_add_subnode(fdt, cpu_name);
        qemu_devtree_setprop_cell(fdt, cpu_name, "clock-frequency", clock_freq);
        qemu_devtree_setprop_cell(fdt, cpu_name, "timebase-frequency", tb_freq);
        qemu_devtree_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_devtree_setprop_cell(fdt, cpu_name, "reg", env->cpu_index);
        qemu_devtree_setprop_cell(fdt, cpu_name, "d-cache-line-size",
                                  env->dcache_line_size);
        qemu_devtree_setprop_cell(fdt, cpu_name, "i-cache-line-size",
                                  env->icache_line_size);
        qemu_devtree_setprop_cell(fdt, cpu_name, "d-cache-size", 0x8000);
        qemu_devtree_setprop_cell(fdt, cpu_name, "i-cache-size", 0x8000);
        qemu_devtree_setprop_cell(fdt, cpu_name, "bus-frequency", 0);
        if (env->cpu_index) {
            qemu_devtree_setprop_string(fdt, cpu_name, "status", "disabled");
            qemu_devtree_setprop_string(fdt, cpu_name, "enable-method", "spin-table");
            qemu_devtree_setprop_u64(fdt, cpu_name, "cpu-release-addr",
                                     cpu_release_addr);
        } else {
            qemu_devtree_setprop_string(fdt, cpu_name, "status", "okay");
        }
    }

    qemu_devtree_add_subnode(fdt, "/aliases");
    /* XXX These should go into their respective devices' code */
    snprintf(soc, sizeof(soc), "/soc@%llx", MPC8544_CCSRBAR_BASE);
    qemu_devtree_add_subnode(fdt, soc);
    qemu_devtree_setprop_string(fdt, soc, "device_type", "soc");
    qemu_devtree_setprop(fdt, soc, "compatible", compatible_sb,
                         sizeof(compatible_sb));
    qemu_devtree_setprop_cell(fdt, soc, "#address-cells", 1);
    qemu_devtree_setprop_cell(fdt, soc, "#size-cells", 1);
    qemu_devtree_setprop_cells(fdt, soc, "ranges", 0x0,
                               MPC8544_CCSRBAR_BASE >> 32, MPC8544_CCSRBAR_BASE,
                               MPC8544_CCSRBAR_SIZE);
    /* XXX should contain a reasonable value */
    qemu_devtree_setprop_cell(fdt, soc, "bus-frequency", 0);

    snprintf(mpic, sizeof(mpic), "%s/pic@%llx", soc,
             MPC8544_MPIC_REGS_BASE - MPC8544_CCSRBAR_BASE);
    qemu_devtree_add_subnode(fdt, mpic);
    qemu_devtree_setprop_string(fdt, mpic, "device_type", "open-pic");
    qemu_devtree_setprop_string(fdt, mpic, "compatible", "chrp,open-pic");
    qemu_devtree_setprop_cells(fdt, mpic, "reg", MPC8544_MPIC_REGS_BASE -
                               MPC8544_CCSRBAR_BASE, 0x40000);
    qemu_devtree_setprop_cell(fdt, mpic, "#address-cells", 0);
    qemu_devtree_setprop_cell(fdt, mpic, "#interrupt-cells", 2);
    mpic_ph = qemu_devtree_alloc_phandle(fdt);
    qemu_devtree_setprop_cell(fdt, mpic, "phandle", mpic_ph);
    qemu_devtree_setprop_cell(fdt, mpic, "linux,phandle", mpic_ph);
    qemu_devtree_setprop(fdt, mpic, "interrupt-controller", NULL, 0);

    /*
     * We have to generate ser1 first, because Linux takes the first
     * device it finds in the dt as serial output device. And we generate
     * devices in reverse order to the dt.
     */
    dt_serial_create(fdt, MPC8544_SERIAL1_REGS_BASE - MPC8544_CCSRBAR_BASE,
                     soc, mpic, "serial1", 1, false);
    dt_serial_create(fdt, MPC8544_SERIAL0_REGS_BASE - MPC8544_CCSRBAR_BASE,
                     soc, mpic, "serial0", 0, true);

    snprintf(gutil, sizeof(gutil), "%s/global-utilities@%llx", soc,
             MPC8544_UTIL_BASE - MPC8544_CCSRBAR_BASE);
    qemu_devtree_add_subnode(fdt, gutil);
    qemu_devtree_setprop_string(fdt, gutil, "compatible", "fsl,mpc8544-guts");
    qemu_devtree_setprop_cells(fdt, gutil, "reg", MPC8544_UTIL_BASE -
                               MPC8544_CCSRBAR_BASE, 0x1000);
    qemu_devtree_setprop(fdt, gutil, "fsl,has-rstcr", NULL, 0);

    snprintf(pci, sizeof(pci), "/pci@%llx", MPC8544_PCI_REGS_BASE);
    qemu_devtree_add_subnode(fdt, pci);
    qemu_devtree_setprop_cell(fdt, pci, "cell-index", 0);
    qemu_devtree_setprop_string(fdt, pci, "compatible", "fsl,mpc8540-pci");
    qemu_devtree_setprop_string(fdt, pci, "device_type", "pci");
    qemu_devtree_setprop_cells(fdt, pci, "interrupt-map-mask", 0xf800, 0x0,
                               0x0, 0x7);
    pci_map_create(fdt, pci_map, qemu_devtree_get_phandle(fdt, mpic));
    qemu_devtree_setprop(fdt, pci, "interrupt-map", pci_map, sizeof(pci_map));
    qemu_devtree_setprop_phandle(fdt, pci, "interrupt-parent", mpic);
    qemu_devtree_setprop_cells(fdt, pci, "interrupts", 24, 2);
    qemu_devtree_setprop_cells(fdt, pci, "bus-range", 0, 255);
    for (i = 0; i < 14; i++) {
        pci_ranges[i] = cpu_to_be32(pci_ranges[i]);
    }
    qemu_devtree_setprop(fdt, pci, "ranges", pci_ranges, sizeof(pci_ranges));
    qemu_devtree_setprop_cells(fdt, pci, "reg", MPC8544_PCI_REGS_BASE >> 32,
                               MPC8544_PCI_REGS_BASE, 0, 0x1000);
    qemu_devtree_setprop_cell(fdt, pci, "clock-frequency", 66666666);
    qemu_devtree_setprop_cell(fdt, pci, "#interrupt-cells", 1);
    qemu_devtree_setprop_cell(fdt, pci, "#size-cells", 2);
    qemu_devtree_setprop_cell(fdt, pci, "#address-cells", 3);
    qemu_devtree_setprop_string(fdt, "/aliases", "pci0", pci);

    params->fixup_devtree(params, fdt);

    if (toplevel_compat) {
        qemu_devtree_setprop(fdt, "/", "compatible", toplevel_compat,
                             strlen(toplevel_compat) + 1);
    }

done:
    if (dumpdtb) {
        /* Dump the dtb to a file and quit */
        FILE *f = fopen(dumpdtb, "wb");
        size_t len;
        len = fwrite(fdt, fdt_size, 1, f);
        fclose(f);
        if (len != fdt_size) {
            exit(1);
        }
        exit(0);
    }

    ret = rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);
    if (ret < 0) {
        goto out;
    }
    g_free(fdt);
    ret = fdt_size;

out:

    return ret;
}

/* Create -kernel TLB entries for BookE.  */
static inline target_phys_addr_t booke206_page_size_to_tlb(uint64_t size)
{
    return 63 - clz64(size >> 10);
}

static void mmubooke_create_initial_mapping(CPUPPCState *env)
{
    struct boot_info *bi = env->load_info;
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 0);
    target_phys_addr_t size, dt_end;
    int ps;

    /* Our initial TLB entry needs to cover everything from 0 to
       the device tree top */
    dt_end = bi->dt_base + bi->dt_size;
    ps = booke206_page_size_to_tlb(dt_end) + 1;
    size = (ps << MAS1_TSIZE_SHIFT);
    tlb->mas1 = MAS1_VALID | size;
    tlb->mas2 = 0;
    tlb->mas7_3 = 0;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;

    env->tlb_dirty = true;
}

static void ppce500_cpu_reset_sec(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    cpu_reset(CPU(cpu));

    /* Secondary CPU starts in halted state for now. Needs to change when
       implementing non-kernel boot. */
    env->halted = 1;
    env->exception_index = EXCP_HLT;
}

static void ppce500_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(CPU(cpu));

    /* Set initial guest state. */
    env->halted = 0;
    env->gpr[1] = (16<<20) - 8;
    env->gpr[3] = bi->dt_base;
    env->nip = bi->entry;
    mmubooke_create_initial_mapping(env);
}

void ppce500_init(PPCE500Params *params)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    PCIBus *pci_bus;
    CPUPPCState *env = NULL;
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
    qemu_irq **irqs, *mpic;
    DeviceState *dev;
    CPUPPCState *firstenv = NULL;

    /* Setup CPUs */
    if (params->cpu_model == NULL) {
        params->cpu_model = "e500v2_v30";
    }

    irqs = g_malloc0(smp_cpus * sizeof(qemu_irq *));
    irqs[0] = g_malloc0(smp_cpus * sizeof(qemu_irq) * OPENPIC_OUTPUT_NB);
    for (i = 0; i < smp_cpus; i++) {
        PowerPCCPU *cpu;
        qemu_irq *input;

        cpu = cpu_ppc_init(params->cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to initialize CPU!\n");
            exit(1);
        }
        env = &cpu->env;

        if (!firstenv) {
            firstenv = env;
        }

        irqs[i] = irqs[0] + (i * OPENPIC_OUTPUT_NB);
        input = (qemu_irq *)env->irq_inputs;
        irqs[i][OPENPIC_OUTPUT_INT] = input[PPCE500_INPUT_INT];
        irqs[i][OPENPIC_OUTPUT_CINT] = input[PPCE500_INPUT_CINT];
        env->spr[SPR_BOOKE_PIR] = env->cpu_index = i;
        env->mpic_cpu_base = MPC8544_MPIC_REGS_BASE + 0x20000;

        ppc_booke_timers_init(env, 400000000, PPC_TIMER_E500);

        /* Register reset handler */
        if (!i) {
            /* Primary CPU */
            struct boot_info *boot_info;
            boot_info = g_malloc0(sizeof(struct boot_info));
            qemu_register_reset(ppce500_cpu_reset, cpu);
            env->load_info = boot_info;
        } else {
            /* Secondary CPUs */
            qemu_register_reset(ppce500_cpu_reset_sec, cpu);
        }
    }

    env = firstenv;

    /* Fixup Memory size on a alignment boundary */
    ram_size &= ~(RAM_SIZES_ALIGN - 1);

    /* Register Memory */
    memory_region_init_ram(ram, "mpc8544ds.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* MPIC */
    mpic = mpic_init(address_space_mem, MPC8544_MPIC_REGS_BASE,
                     smp_cpus, irqs, NULL);

    if (!mpic) {
        cpu_abort(env, "MPIC failed to initialize\n");
    }

    /* Serial */
    if (serial_hds[0]) {
        serial_mm_init(address_space_mem, MPC8544_SERIAL0_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], DEVICE_BIG_ENDIAN);
    }

    if (serial_hds[1]) {
        serial_mm_init(address_space_mem, MPC8544_SERIAL1_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], DEVICE_BIG_ENDIAN);
    }

    /* General Utility device */
    sysbus_create_simple("mpc8544-guts", MPC8544_UTIL_BASE, NULL);

    /* PCI */
    dev = sysbus_create_varargs("e500-pcihost", MPC8544_PCI_REGS_BASE,
                                mpic[pci_irq_nrs[0]], mpic[pci_irq_nrs[1]],
                                mpic[pci_irq_nrs[2]], mpic[pci_irq_nrs[3]],
                                NULL);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pci_bus)
        printf("couldn't create PCI controller!\n");

    isa_mmio_init(MPC8544_PCI_IO, MPC8544_PCI_IOLEN);

    if (pci_bus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], "virtio", NULL);
        }
    }

    /* Register spinning region */
    sysbus_create_simple("e500-spin", MPC8544_SPIN_BASE, NULL);

    /* Load kernel. */
    if (params->kernel_filename) {
        kernel_size = load_uimage(params->kernel_filename, &entry,
                                  &loadaddr, NULL);
        if (kernel_size < 0) {
            kernel_size = load_elf(params->kernel_filename, NULL, NULL,
                                   &elf_entry, &elf_lowaddr, NULL, 1,
                                   ELF_MACHINE, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    params->kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (params->initrd_filename) {
        initrd_base = (kernel_size + INITRD_LOAD_PAD) & ~INITRD_PAD_MASK;
        initrd_size = load_image_targphys(params->initrd_filename, initrd_base,
                                          ram_size - initrd_base);

        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    params->initrd_filename);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (params->kernel_filename) {
        struct boot_info *boot_info;
        int dt_size;

        dt_base = (loadaddr + kernel_size + DTC_LOAD_PAD) & ~DTC_PAD_MASK;
        dt_size = ppce500_load_device_tree(env, params, dt_base, initrd_base,
                                           initrd_size);
        if (dt_size < 0) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }

        boot_info = env->load_info;
        boot_info->entry = entry;
        boot_info->dt_base = dt_base;
        boot_info->dt_size = dt_size;
    }

    if (kvm_enabled()) {
        kvmppc_init();
    }
}
