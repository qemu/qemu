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
#include "e500-ccsr.h"
#include "net/net.h"
#include "qemu/config-file.h"
#include "hw/hw.h"
#include "hw/char/serial.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "hw/ppc/openpic.h"
#include "hw/ppc/ppc.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "qemu/host-utils.h"
#include "hw/pci-host/ppce500.h"

#define EPAPR_MAGIC                (0x45504150)
#define BINARY_DEVICE_TREE_FILE    "mpc8544ds.dtb"
#define UIMAGE_LOAD_BASE           0
#define DTC_LOAD_PAD               0x1800000
#define DTC_PAD_MASK               0xFFFFF
#define DTB_MAX_SIZE               (8 * 1024 * 1024)
#define INITRD_LOAD_PAD            0x2000000
#define INITRD_PAD_MASK            0xFFFFFF

#define RAM_SIZES_ALIGN            (64UL << 20)

/* TODO: parameterize */
#define MPC8544_CCSRBAR_BASE       0xE0000000ULL
#define MPC8544_CCSRBAR_SIZE       0x00100000ULL
#define MPC8544_MPIC_REGS_OFFSET   0x40000ULL
#define MPC8544_MSI_REGS_OFFSET   0x41600ULL
#define MPC8544_SERIAL0_REGS_OFFSET 0x4500ULL
#define MPC8544_SERIAL1_REGS_OFFSET 0x4600ULL
#define MPC8544_PCI_REGS_OFFSET    0x8000ULL
#define MPC8544_PCI_REGS_BASE      (MPC8544_CCSRBAR_BASE + \
                                    MPC8544_PCI_REGS_OFFSET)
#define MPC8544_PCI_REGS_SIZE      0x1000ULL
#define MPC8544_PCI_IO             0xE1000000ULL
#define MPC8544_UTIL_OFFSET        0xe0000ULL
#define MPC8544_SPIN_BASE          0xEF000000ULL

struct boot_info
{
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};

static uint32_t *pci_map_create(void *fdt, uint32_t mpic, int first_slot,
                                int nr_slots, int *len)
{
    int i = 0;
    int slot;
    int pci_irq;
    int host_irq;
    int last_slot = first_slot + nr_slots;
    uint32_t *pci_map;

    *len = nr_slots * 4 * 7 * sizeof(uint32_t);
    pci_map = g_malloc(*len);

    for (slot = first_slot; slot < last_slot; slot++) {
        for (pci_irq = 0; pci_irq < 4; pci_irq++) {
            pci_map[i++] = cpu_to_be32(slot << 11);
            pci_map[i++] = cpu_to_be32(0x0);
            pci_map[i++] = cpu_to_be32(0x0);
            pci_map[i++] = cpu_to_be32(pci_irq + 1);
            pci_map[i++] = cpu_to_be32(mpic);
            host_irq = ppce500_pci_map_irq_slot(slot, pci_irq);
            pci_map[i++] = cpu_to_be32(host_irq + 1);
            pci_map[i++] = cpu_to_be32(0x1);
        }
    }

    assert((i * sizeof(uint32_t)) == *len);

    return pci_map;
}

static void dt_serial_create(void *fdt, unsigned long long offset,
                             const char *soc, const char *mpic,
                             const char *alias, int idx, bool defcon)
{
    char ser[128];

    snprintf(ser, sizeof(ser), "%s/serial@%llx", soc, offset);
    qemu_fdt_add_subnode(fdt, ser);
    qemu_fdt_setprop_string(fdt, ser, "device_type", "serial");
    qemu_fdt_setprop_string(fdt, ser, "compatible", "ns16550");
    qemu_fdt_setprop_cells(fdt, ser, "reg", offset, 0x100);
    qemu_fdt_setprop_cell(fdt, ser, "cell-index", idx);
    qemu_fdt_setprop_cell(fdt, ser, "clock-frequency", 0);
    qemu_fdt_setprop_cells(fdt, ser, "interrupts", 42, 2);
    qemu_fdt_setprop_phandle(fdt, ser, "interrupt-parent", mpic);
    qemu_fdt_setprop_string(fdt, "/aliases", alias, ser);

    if (defcon) {
        qemu_fdt_setprop_string(fdt, "/chosen", "linux,stdout-path", ser);
    }
}

static int ppce500_load_device_tree(QEMUMachineInitArgs *args,
                                    PPCE500Params *params,
                                    hwaddr addr,
                                    hwaddr initrd_base,
                                    hwaddr initrd_size,
                                    bool dry_run)
{
    CPUPPCState *env = first_cpu->env_ptr;
    int ret = -1;
    uint64_t mem_reg_property[] = { 0, cpu_to_be64(args->ram_size) };
    int fdt_size;
    void *fdt;
    uint8_t hypercall[16];
    uint32_t clock_freq = 400000000;
    uint32_t tb_freq = 400000000;
    int i;
    char compatible_sb[] = "fsl,mpc8544-immr\0simple-bus";
    char soc[128];
    char mpic[128];
    uint32_t mpic_ph;
    uint32_t msi_ph;
    char gutil[128];
    char pci[128];
    char msi[128];
    uint32_t *pci_map = NULL;
    int len;
    uint32_t pci_ranges[14] =
        {
            0x2000000, 0x0, 0xc0000000,
            0x0, 0xc0000000,
            0x0, 0x20000000,

            0x1000000, 0x0, 0x0,
            0x0, 0xe1000000,
            0x0, 0x10000,
        };
    QemuOpts *machine_opts = qemu_get_machine_opts();
    const char *dtb_file = qemu_opt_get(machine_opts, "dtb");
    const char *toplevel_compat = qemu_opt_get(machine_opts, "dt_compatible");

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
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 2);

    qemu_fdt_add_subnode(fdt, "/memory");
    qemu_fdt_setprop_string(fdt, "/memory", "device_type", "memory");
    qemu_fdt_setprop(fdt, "/memory", "reg", mem_reg_property,
                     sizeof(mem_reg_property));

    qemu_fdt_add_subnode(fdt, "/chosen");
    if (initrd_size) {
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
    }

    ret = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                      args->kernel_cmdline);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");

    if (kvm_enabled()) {
        /* Read out host's frequencies */
        clock_freq = kvmppc_get_clockfreq();
        tb_freq = kvmppc_get_tbfreq();

        /* indicate KVM hypercall interface */
        qemu_fdt_add_subnode(fdt, "/hypervisor");
        qemu_fdt_setprop_string(fdt, "/hypervisor", "compatible",
                                "linux,kvm");
        kvmppc_get_hypercall(env, hypercall, sizeof(hypercall));
        qemu_fdt_setprop(fdt, "/hypervisor", "hcall-instructions",
                         hypercall, sizeof(hypercall));
        /* if KVM supports the idle hcall, set property indicating this */
        if (kvmppc_get_hasidle(env)) {
            qemu_fdt_setprop(fdt, "/hypervisor", "has-idle", NULL, 0);
        }
    }

    /* Create CPU nodes */
    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0);

    /* We need to generate the cpu nodes in reverse order, so Linux can pick
       the first node as boot node and be happy */
    for (i = smp_cpus - 1; i >= 0; i--) {
        CPUState *cpu;
        PowerPCCPU *pcpu;
        char cpu_name[128];
        uint64_t cpu_release_addr = MPC8544_SPIN_BASE + (i * 0x20);

        cpu = qemu_get_cpu(i);
        if (cpu == NULL) {
            continue;
        }
        env = cpu->env_ptr;
        pcpu = POWERPC_CPU(cpu);

        snprintf(cpu_name, sizeof(cpu_name), "/cpus/PowerPC,8544@%x",
                 ppc_get_vcpu_dt_id(pcpu));
        qemu_fdt_add_subnode(fdt, cpu_name);
        qemu_fdt_setprop_cell(fdt, cpu_name, "clock-frequency", clock_freq);
        qemu_fdt_setprop_cell(fdt, cpu_name, "timebase-frequency", tb_freq);
        qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_fdt_setprop_cell(fdt, cpu_name, "reg",
                              ppc_get_vcpu_dt_id(pcpu));
        qemu_fdt_setprop_cell(fdt, cpu_name, "d-cache-line-size",
                              env->dcache_line_size);
        qemu_fdt_setprop_cell(fdt, cpu_name, "i-cache-line-size",
                              env->icache_line_size);
        qemu_fdt_setprop_cell(fdt, cpu_name, "d-cache-size", 0x8000);
        qemu_fdt_setprop_cell(fdt, cpu_name, "i-cache-size", 0x8000);
        qemu_fdt_setprop_cell(fdt, cpu_name, "bus-frequency", 0);
        if (cpu->cpu_index) {
            qemu_fdt_setprop_string(fdt, cpu_name, "status", "disabled");
            qemu_fdt_setprop_string(fdt, cpu_name, "enable-method",
                                    "spin-table");
            qemu_fdt_setprop_u64(fdt, cpu_name, "cpu-release-addr",
                                 cpu_release_addr);
        } else {
            qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
        }
    }

    qemu_fdt_add_subnode(fdt, "/aliases");
    /* XXX These should go into their respective devices' code */
    snprintf(soc, sizeof(soc), "/soc@%llx", MPC8544_CCSRBAR_BASE);
    qemu_fdt_add_subnode(fdt, soc);
    qemu_fdt_setprop_string(fdt, soc, "device_type", "soc");
    qemu_fdt_setprop(fdt, soc, "compatible", compatible_sb,
                     sizeof(compatible_sb));
    qemu_fdt_setprop_cell(fdt, soc, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, soc, "#size-cells", 1);
    qemu_fdt_setprop_cells(fdt, soc, "ranges", 0x0,
                           MPC8544_CCSRBAR_BASE >> 32, MPC8544_CCSRBAR_BASE,
                           MPC8544_CCSRBAR_SIZE);
    /* XXX should contain a reasonable value */
    qemu_fdt_setprop_cell(fdt, soc, "bus-frequency", 0);

    snprintf(mpic, sizeof(mpic), "%s/pic@%llx", soc, MPC8544_MPIC_REGS_OFFSET);
    qemu_fdt_add_subnode(fdt, mpic);
    qemu_fdt_setprop_string(fdt, mpic, "device_type", "open-pic");
    qemu_fdt_setprop_string(fdt, mpic, "compatible", "fsl,mpic");
    qemu_fdt_setprop_cells(fdt, mpic, "reg", MPC8544_MPIC_REGS_OFFSET,
                           0x40000);
    qemu_fdt_setprop_cell(fdt, mpic, "#address-cells", 0);
    qemu_fdt_setprop_cell(fdt, mpic, "#interrupt-cells", 2);
    mpic_ph = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_setprop_cell(fdt, mpic, "phandle", mpic_ph);
    qemu_fdt_setprop_cell(fdt, mpic, "linux,phandle", mpic_ph);
    qemu_fdt_setprop(fdt, mpic, "interrupt-controller", NULL, 0);

    /*
     * We have to generate ser1 first, because Linux takes the first
     * device it finds in the dt as serial output device. And we generate
     * devices in reverse order to the dt.
     */
    dt_serial_create(fdt, MPC8544_SERIAL1_REGS_OFFSET,
                     soc, mpic, "serial1", 1, false);
    dt_serial_create(fdt, MPC8544_SERIAL0_REGS_OFFSET,
                     soc, mpic, "serial0", 0, true);

    snprintf(gutil, sizeof(gutil), "%s/global-utilities@%llx", soc,
             MPC8544_UTIL_OFFSET);
    qemu_fdt_add_subnode(fdt, gutil);
    qemu_fdt_setprop_string(fdt, gutil, "compatible", "fsl,mpc8544-guts");
    qemu_fdt_setprop_cells(fdt, gutil, "reg", MPC8544_UTIL_OFFSET, 0x1000);
    qemu_fdt_setprop(fdt, gutil, "fsl,has-rstcr", NULL, 0);

    snprintf(msi, sizeof(msi), "/%s/msi@%llx", soc, MPC8544_MSI_REGS_OFFSET);
    qemu_fdt_add_subnode(fdt, msi);
    qemu_fdt_setprop_string(fdt, msi, "compatible", "fsl,mpic-msi");
    qemu_fdt_setprop_cells(fdt, msi, "reg", MPC8544_MSI_REGS_OFFSET, 0x200);
    msi_ph = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_setprop_cells(fdt, msi, "msi-available-ranges", 0x0, 0x100);
    qemu_fdt_setprop_phandle(fdt, msi, "interrupt-parent", mpic);
    qemu_fdt_setprop_cells(fdt, msi, "interrupts",
        0xe0, 0x0,
        0xe1, 0x0,
        0xe2, 0x0,
        0xe3, 0x0,
        0xe4, 0x0,
        0xe5, 0x0,
        0xe6, 0x0,
        0xe7, 0x0);
    qemu_fdt_setprop_cell(fdt, msi, "phandle", msi_ph);
    qemu_fdt_setprop_cell(fdt, msi, "linux,phandle", msi_ph);

    snprintf(pci, sizeof(pci), "/pci@%llx", MPC8544_PCI_REGS_BASE);
    qemu_fdt_add_subnode(fdt, pci);
    qemu_fdt_setprop_cell(fdt, pci, "cell-index", 0);
    qemu_fdt_setprop_string(fdt, pci, "compatible", "fsl,mpc8540-pci");
    qemu_fdt_setprop_string(fdt, pci, "device_type", "pci");
    qemu_fdt_setprop_cells(fdt, pci, "interrupt-map-mask", 0xf800, 0x0,
                           0x0, 0x7);
    pci_map = pci_map_create(fdt, qemu_fdt_get_phandle(fdt, mpic),
                             params->pci_first_slot, params->pci_nr_slots,
                             &len);
    qemu_fdt_setprop(fdt, pci, "interrupt-map", pci_map, len);
    qemu_fdt_setprop_phandle(fdt, pci, "interrupt-parent", mpic);
    qemu_fdt_setprop_cells(fdt, pci, "interrupts", 24, 2);
    qemu_fdt_setprop_cells(fdt, pci, "bus-range", 0, 255);
    for (i = 0; i < 14; i++) {
        pci_ranges[i] = cpu_to_be32(pci_ranges[i]);
    }
    qemu_fdt_setprop_cell(fdt, pci, "fsl,msi", msi_ph);
    qemu_fdt_setprop(fdt, pci, "ranges", pci_ranges, sizeof(pci_ranges));
    qemu_fdt_setprop_cells(fdt, pci, "reg", MPC8544_PCI_REGS_BASE >> 32,
                           MPC8544_PCI_REGS_BASE, 0, 0x1000);
    qemu_fdt_setprop_cell(fdt, pci, "clock-frequency", 66666666);
    qemu_fdt_setprop_cell(fdt, pci, "#interrupt-cells", 1);
    qemu_fdt_setprop_cell(fdt, pci, "#size-cells", 2);
    qemu_fdt_setprop_cell(fdt, pci, "#address-cells", 3);
    qemu_fdt_setprop_string(fdt, "/aliases", "pci0", pci);

    params->fixup_devtree(params, fdt);

    if (toplevel_compat) {
        qemu_fdt_setprop(fdt, "/", "compatible", toplevel_compat,
                         strlen(toplevel_compat) + 1);
    }

done:
    if (!dry_run) {
        qemu_fdt_dumpdtb(fdt, fdt_size);
        cpu_physical_memory_write(addr, fdt, fdt_size);
    }
    ret = fdt_size;

out:
    g_free(pci_map);

    return ret;
}

typedef struct DeviceTreeParams {
    QEMUMachineInitArgs args;
    PPCE500Params params;
    hwaddr addr;
    hwaddr initrd_base;
    hwaddr initrd_size;
} DeviceTreeParams;

static void ppce500_reset_device_tree(void *opaque)
{
    DeviceTreeParams *p = opaque;
    ppce500_load_device_tree(&p->args, &p->params, p->addr, p->initrd_base,
                             p->initrd_size, false);
}

static int ppce500_prep_device_tree(QEMUMachineInitArgs *args,
                                    PPCE500Params *params,
                                    hwaddr addr,
                                    hwaddr initrd_base,
                                    hwaddr initrd_size)
{
    DeviceTreeParams *p = g_new(DeviceTreeParams, 1);
    p->args = *args;
    p->params = *params;
    p->addr = addr;
    p->initrd_base = initrd_base;
    p->initrd_size = initrd_size;

    qemu_register_reset(ppce500_reset_device_tree, p);

    /* Issue the device tree loader once, so that we get the size of the blob */
    return ppce500_load_device_tree(args, params, addr, initrd_base,
                                    initrd_size, true);
}

/* Create -kernel TLB entries for BookE.  */
static inline hwaddr booke206_page_size_to_tlb(uint64_t size)
{
    return 63 - clz64(size >> 10);
}

static int booke206_initial_map_tsize(CPUPPCState *env)
{
    struct boot_info *bi = env->load_info;
    hwaddr dt_end;
    int ps;

    /* Our initial TLB entry needs to cover everything from 0 to
       the device tree top */
    dt_end = bi->dt_base + bi->dt_size;
    ps = booke206_page_size_to_tlb(dt_end) + 1;
    if (ps & 1) {
        /* e500v2 can only do even TLB size bits */
        ps++;
    }
    return ps;
}

static uint64_t mmubooke_initial_mapsize(CPUPPCState *env)
{
    int tsize;

    tsize = booke206_initial_map_tsize(env);
    return (1ULL << 10 << tsize);
}

static void mmubooke_create_initial_mapping(CPUPPCState *env)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 0);
    hwaddr size;
    int ps;

    ps = booke206_initial_map_tsize(env);
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
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);

    /* Secondary CPU starts in halted state for now. Needs to change when
       implementing non-kernel boot. */
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
}

static void ppce500_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(cs);

    /* Set initial guest state. */
    cs->halted = 0;
    env->gpr[1] = (16<<20) - 8;
    env->gpr[3] = bi->dt_base;
    env->gpr[4] = 0;
    env->gpr[5] = 0;
    env->gpr[6] = EPAPR_MAGIC;
    env->gpr[7] = mmubooke_initial_mapsize(env);
    env->gpr[8] = 0;
    env->gpr[9] = 0;
    env->nip = bi->entry;
    mmubooke_create_initial_mapping(env);
}

static DeviceState *ppce500_init_mpic_qemu(PPCE500Params *params,
                                           qemu_irq **irqs)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i, j, k;

    dev = qdev_create(NULL, TYPE_OPENPIC);
    qdev_prop_set_uint32(dev, "model", params->mpic_version);
    qdev_prop_set_uint32(dev, "nb_cpus", smp_cpus);

    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

    k = 0;
    for (i = 0; i < smp_cpus; i++) {
        for (j = 0; j < OPENPIC_OUTPUT_NB; j++) {
            sysbus_connect_irq(s, k++, irqs[i][j]);
        }
    }

    return dev;
}

static DeviceState *ppce500_init_mpic_kvm(PPCE500Params *params,
                                          qemu_irq **irqs)
{
    DeviceState *dev;
    CPUState *cs;
    int r;

    dev = qdev_create(NULL, TYPE_KVM_OPENPIC);
    qdev_prop_set_uint32(dev, "model", params->mpic_version);

    r = qdev_init(dev);
    if (r) {
        return NULL;
    }

    CPU_FOREACH(cs) {
        if (kvm_openpic_connect_vcpu(dev, cs)) {
            fprintf(stderr, "%s: failed to connect vcpu to irqchip\n",
                    __func__);
            abort();
        }
    }

    return dev;
}

static qemu_irq *ppce500_init_mpic(PPCE500Params *params, MemoryRegion *ccsr,
                                   qemu_irq **irqs)
{
    qemu_irq *mpic;
    DeviceState *dev = NULL;
    SysBusDevice *s;
    int i;

    mpic = g_new(qemu_irq, 256);

    if (kvm_enabled()) {
        QemuOpts *machine_opts = qemu_get_machine_opts();
        bool irqchip_allowed = qemu_opt_get_bool(machine_opts,
                                                "kernel_irqchip", true);
        bool irqchip_required = qemu_opt_get_bool(machine_opts,
                                                  "kernel_irqchip", false);

        if (irqchip_allowed) {
            dev = ppce500_init_mpic_kvm(params, irqs);
        }

        if (irqchip_required && !dev) {
            fprintf(stderr, "%s: irqchip requested but unavailable\n",
                    __func__);
            abort();
        }
    }

    if (!dev) {
        dev = ppce500_init_mpic_qemu(params, irqs);
    }

    for (i = 0; i < 256; i++) {
        mpic[i] = qdev_get_gpio_in(dev, i);
    }

    s = SYS_BUS_DEVICE(dev);
    memory_region_add_subregion(ccsr, MPC8544_MPIC_REGS_OFFSET,
                                s->mmio[0].memory);

    return mpic;
}

void ppce500_init(QEMUMachineInitArgs *args, PPCE500Params *params)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    PCIBus *pci_bus;
    CPUPPCState *env = NULL;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    hwaddr entry=0;
    hwaddr loadaddr=UIMAGE_LOAD_BASE;
    target_long kernel_size=0;
    target_ulong dt_base = 0;
    target_ulong initrd_base = 0;
    target_long initrd_size = 0;
    target_ulong cur_base = 0;
    int i;
    unsigned int pci_irq_nrs[4] = {1, 2, 3, 4};
    qemu_irq **irqs, *mpic;
    DeviceState *dev;
    CPUPPCState *firstenv = NULL;
    MemoryRegion *ccsr_addr_space;
    SysBusDevice *s;
    PPCE500CCSRState *ccsr;

    /* Setup CPUs */
    if (args->cpu_model == NULL) {
        args->cpu_model = "e500v2_v30";
    }

    irqs = g_malloc0(smp_cpus * sizeof(qemu_irq *));
    irqs[0] = g_malloc0(smp_cpus * sizeof(qemu_irq) * OPENPIC_OUTPUT_NB);
    for (i = 0; i < smp_cpus; i++) {
        PowerPCCPU *cpu;
        CPUState *cs;
        qemu_irq *input;

        cpu = cpu_ppc_init(args->cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to initialize CPU!\n");
            exit(1);
        }
        env = &cpu->env;
        cs = CPU(cpu);

        if (!firstenv) {
            firstenv = env;
        }

        irqs[i] = irqs[0] + (i * OPENPIC_OUTPUT_NB);
        input = (qemu_irq *)env->irq_inputs;
        irqs[i][OPENPIC_OUTPUT_INT] = input[PPCE500_INPUT_INT];
        irqs[i][OPENPIC_OUTPUT_CINT] = input[PPCE500_INPUT_CINT];
        env->spr_cb[SPR_BOOKE_PIR].default_value = cs->cpu_index = i;
        env->mpic_iack = MPC8544_CCSRBAR_BASE +
                         MPC8544_MPIC_REGS_OFFSET + 0xa0;

        ppc_booke_timers_init(cpu, 400000000, PPC_TIMER_E500);

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
    args->ram_size = ram_size;

    /* Register Memory */
    memory_region_init_ram(ram, NULL, "mpc8544ds.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, 0, ram);

    dev = qdev_create(NULL, "e500-ccsr");
    object_property_add_child(qdev_get_machine(), "e500-ccsr",
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);
    ccsr = CCSR(dev);
    ccsr_addr_space = &ccsr->ccsr_space;
    memory_region_add_subregion(address_space_mem, MPC8544_CCSRBAR_BASE,
                                ccsr_addr_space);

    mpic = ppce500_init_mpic(params, ccsr_addr_space, irqs);

    /* Serial */
    if (serial_hds[0]) {
        serial_mm_init(ccsr_addr_space, MPC8544_SERIAL0_REGS_OFFSET,
                       0, mpic[42], 399193,
                       serial_hds[0], DEVICE_BIG_ENDIAN);
    }

    if (serial_hds[1]) {
        serial_mm_init(ccsr_addr_space, MPC8544_SERIAL1_REGS_OFFSET,
                       0, mpic[42], 399193,
                       serial_hds[1], DEVICE_BIG_ENDIAN);
    }

    /* General Utility device */
    dev = qdev_create(NULL, "mpc8544-guts");
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    memory_region_add_subregion(ccsr_addr_space, MPC8544_UTIL_OFFSET,
                                sysbus_mmio_get_region(s, 0));

    /* PCI */
    dev = qdev_create(NULL, "e500-pcihost");
    qdev_prop_set_uint32(dev, "first_slot", params->pci_first_slot);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, mpic[pci_irq_nrs[0]]);
    sysbus_connect_irq(s, 1, mpic[pci_irq_nrs[1]]);
    sysbus_connect_irq(s, 2, mpic[pci_irq_nrs[2]]);
    sysbus_connect_irq(s, 3, mpic[pci_irq_nrs[3]]);
    memory_region_add_subregion(ccsr_addr_space, MPC8544_PCI_REGS_OFFSET,
                                sysbus_mmio_get_region(s, 0));

    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pci_bus)
        printf("couldn't create PCI controller!\n");

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, MPC8544_PCI_IO);

    if (pci_bus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], pci_bus, "virtio", NULL);
        }
    }

    /* Register spinning region */
    sysbus_create_simple("e500-spin", MPC8544_SPIN_BASE, NULL);

    /* Load kernel. */
    if (args->kernel_filename) {
        kernel_size = load_uimage(args->kernel_filename, &entry,
                                  &loadaddr, NULL);
        if (kernel_size < 0) {
            kernel_size = load_elf(args->kernel_filename, NULL, NULL,
                                   &elf_entry, &elf_lowaddr, NULL, 1,
                                   ELF_MACHINE, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    args->kernel_filename);
            exit(1);
        }

        cur_base = loadaddr + kernel_size;

        /* Reserve space for dtb */
        dt_base = (cur_base + DTC_LOAD_PAD) & ~DTC_PAD_MASK;
        cur_base += DTB_MAX_SIZE;
    }

    /* Load initrd. */
    if (args->initrd_filename) {
        initrd_base = (cur_base + INITRD_LOAD_PAD) & ~INITRD_PAD_MASK;
        initrd_size = load_image_targphys(args->initrd_filename, initrd_base,
                                          ram_size - initrd_base);

        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    args->initrd_filename);
            exit(1);
        }

        cur_base = initrd_base + initrd_size;
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (args->kernel_filename) {
        struct boot_info *boot_info;
        int dt_size;

        dt_size = ppce500_prep_device_tree(args, params, dt_base,
                                           initrd_base, initrd_size);
        if (dt_size < 0) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }
        assert(dt_size < DTB_MAX_SIZE);

        boot_info = env->load_info;
        boot_info->entry = entry;
        boot_info->dt_base = dt_base;
        boot_info->dt_size = dt_size;
    }

    if (kvm_enabled()) {
        kvmppc_init();
    }
}

static int e500_ccsr_initfn(SysBusDevice *dev)
{
    PPCE500CCSRState *ccsr;

    ccsr = CCSR(dev);
    memory_region_init(&ccsr->ccsr_space, OBJECT(ccsr), "e500-ccsr",
                       MPC8544_CCSRBAR_SIZE);
    return 0;
}

static void e500_ccsr_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = e500_ccsr_initfn;
}

static const TypeInfo e500_ccsr_info = {
    .name          = TYPE_CCSR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PPCE500CCSRState),
    .class_init    = e500_ccsr_class_init,
};

static void e500_register_types(void)
{
    type_register_static(&e500_ccsr_info);
}

type_init(e500_register_types)
