/*
 * Hexagon virt emulation
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "hw/char/pl011.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/hexagon/virt.h"
#include "hw/hexagon/hexagon.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"
#include "sysemu/device_tree.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "elf.h"
#include "machine_cfg_v68n_1024.h.inc"
#include <libfdt.h>

static const int VIRTIO_DEV_COUNT = 2;

static const MemMapEntry base_memmap[] = {

    [VIRT_UART0] = { 0x10000000, 0x00000200 },
    [VIRT_MMIO] = { 0x11000000, 0x00000100 },
    [VIRT_GPT] = { 0xab000000, 0x00001000 },
    [VIRT_FDT] = { 0x99900000, 0x00000200 },
};

static const int irqmap[] = {
    [VIRT_MMIO] = 8, /* ...to 8 + VIRTIO_DEV_COUNT - 1 */
    [VIRT_GPT] = 12,
    [VIRT_UART0] = 50,
};


static void create_fdt(HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    void *fdt = create_device_tree(&vms->fdt_size);

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    ms->fdt = fdt;

    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,hexagon-virt");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_string(fdt, "/", "model", "linux,hexagon-virt");

    qemu_fdt_setprop_string(fdt, "/", "model", "hexagon-virt,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "qcom,sm8150");

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);

    qemu_fdt_add_subnode(fdt, "/chosen");

    uint8_t rng_seed[32];
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static void fdt_add_hvx(HexagonVirtMachineState *vms,
                              const hexagon_machine_config *m_cfg, Error **errp)
{
    const MachineState *ms = MACHINE(vms);
    uint32_t vtcm_size_bytes = m_cfg->cfgtable.vtcm_size_kb * 1024;
    if (vtcm_size_bytes > 0) {
        memory_region_init_ram(&vms->vtcm, NULL, "vtcm.ram", vtcm_size_bytes,
                               errp);
        memory_region_add_subregion(vms->sys, m_cfg->cfgtable.vtcm_base,
                                    &vms->vtcm);

        qemu_fdt_add_subnode(ms->fdt, "/soc/vtcm");
        qemu_fdt_setprop_string(ms->fdt, "/soc/vtcm", "compatible",
                                "qcom,hexagon_vtcm");

        assert(sizeof(m_cfg->cfgtable.vtcm_base) == sizeof(uint32_t));
        qemu_fdt_setprop_cells(ms->fdt, "/soc/vtcm", "reg", 0,
                               m_cfg->cfgtable.vtcm_base, vtcm_size_bytes);
    }

    if (m_cfg->cfgtable.ext_contexts > 0) {
        qemu_fdt_add_subnode(ms->fdt, "/soc/hvx");
        qemu_fdt_setprop_string(ms->fdt, "/soc/hvx", "compatible",
                "qcom,hexagon-hvx");
        qemu_fdt_setprop_cells(ms->fdt, "/soc/hvx", "qcom,hvx-max-ctxts",
                m_cfg->cfgtable.ext_contexts);
        qemu_fdt_setprop_cells(ms->fdt, "/soc/hvx", "qcom,hvx-vlength",
                m_cfg->cfgtable.hvx_vec_log_length);
    }

}

static int32_t irq_hvm_ic_phandle = -1;
static void fdt_add_hvm_pic_node(HexagonVirtMachineState *vms,
                                 const hexagon_machine_config *m_cfg)
{
    MachineState *ms = MACHINE(vms);
    irq_hvm_ic_phandle = qemu_fdt_alloc_phandle(ms->fdt);

    qemu_fdt_setprop_cell(ms->fdt, "/soc", "interrupt-parent",
                          irq_hvm_ic_phandle);

    qemu_fdt_add_subnode(ms->fdt, "/soc/interrupt-controller");
    qemu_fdt_setprop_cell(ms->fdt, "/soc/interrupt-controller",
                          "#address-cells", 2);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/interrupt-controller",
                          "#interrupt-cells", 2);
    qemu_fdt_setprop_string(ms->fdt, "/soc/interrupt-controller", "compatible",
                            "qcom,h2-pic,hvm-pic");
    qemu_fdt_setprop(ms->fdt, "/soc/interrupt-controller",
                     "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, "/soc/interrupt-controller", "phandle",
                          irq_hvm_ic_phandle);

    sysbus_mmio_map(SYS_BUS_DEVICE(vms->l2vic), 1,
                    m_cfg->cfgtable.fastl2vic_base);
}


static void fdt_add_gpt_node(HexagonVirtMachineState *vms)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(vms);

    name = g_strdup_printf("/soc/gpt@%" PRIx64,
        (int64_t) base_memmap[VIRT_GPT].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
                            "qcom,h2-timer,hvm-timer");
    qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", irqmap[VIRT_GPT], 0);
    qemu_fdt_setprop_cells(ms->fdt, name, "reg", 0x0,
                           base_memmap[VIRT_GPT].base,
                           base_memmap[VIRT_GPT].size);
}

static int32_t clock_phandle = -1;
static void fdt_add_clocks(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    clock_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    qemu_fdt_add_subnode(ms->fdt, "/apb-pclk");
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "clock-frequency", 24000000);
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "clock-output-names",
                            "clk24mhz");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "phandle", clock_phandle);
}

static void fdt_add_uart(const HexagonVirtMachineState *vms, int uart)
{
    char *nodename;
    hwaddr base = base_memmap[uart].base;
    hwaddr size = base_memmap[uart].size;
    assert(uart == 0);
    int irq = irqmap[VIRT_UART0 + uart];
    const char compat[] = "arm,pl011\0arm,primecell";
    const char clocknames[] = "uartclk\0apb_pclk";
    MachineState *ms = MACHINE(vms);

    pl011_create(base, qdev_get_gpio_in(vms->l2vic, irq), serial_hd(0));

    nodename = g_strdup_printf("/pl011@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);

    /* Note that we can't use setprop_string because of the embedded NUL */
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, base, size);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts", irq, 0);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "clocks", clock_phandle,
                           clock_phandle);
    qemu_fdt_setprop(ms->fdt, nodename, "clock-names", clocknames,
                     sizeof(clocknames));
    qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                          irq_hvm_ic_phandle);

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    qemu_fdt_add_subnode(ms->fdt, "/aliases");
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", nodename);

    g_free(nodename);
}

static void fdt_add_cpu_nodes(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    /* cpu nodes */
    for (int num = ms->smp.cpus - 1; num >= 0; num--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", num);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_cell(ms->fdt, nodename, "reg", num);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle",
                              qemu_fdt_alloc_phandle(ms->fdt));
        g_free(nodename);
    }
}


static void fdt_add_virtio_devices(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    /* VirtIO MMIO devices */
    for (int i = 0; i < VIRTIO_DEV_COUNT; i++) {
        char *nodename;
        int irq = irqmap[VIRT_MMIO] + i;
        size_t size = base_memmap[VIRT_MMIO].size;
        hwaddr base = base_memmap[VIRT_MMIO].base + i * size;

        nodename = g_strdup_printf("/virtio_mmio@%" PRIx64, base);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg", 2, base, 1,
                                     size);
        qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts", irq, 0);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                              irq_hvm_ic_phandle);

        sysbus_create_simple(
            "virtio-mmio", base + i * size,
            qdev_get_gpio_in(vms->l2vic, irqmap[VIRT_MMIO] + i));

        g_free(nodename);
    }
}

static void virt_instance_init(Object *obj)
{
    HexagonVirtMachineState *vms = HEXAGON_VIRT_MACHINE(obj);

    create_fdt(vms);
}

void hexagon_load_fdt(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    hwaddr fdt_addr = base_memmap[VIRT_FDT].base;
    uint32_t fdtsize = vms->fdt_size;

    /* copy in the device tree */
    qemu_fdt_dumpdtb(ms->fdt, fdtsize);

    rom_add_blob_fixed_as("fdt", ms->fdt, fdtsize, fdt_addr,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(
        qemu_fdt_randomize_seeds,
        rom_ptr_for_as(&address_space_memory, fdt_addr, fdtsize));
}

static uint64_t load_kernel(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    uint64_t entry = 0;
    if (load_elf_ram_sym(
             ms->kernel_filename, NULL, NULL, NULL, NULL, &entry, NULL, NULL, 0,
             EM_HEXAGON, 0, 0, &address_space_memory, false, NULL) > 0) {
        return entry;
    }
    error_report("error loading '%s'", ms->kernel_filename);
    exit(1);
}

static void do_cpu_reset(void *opaque)
{
    HexagonCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    cpu_reset(cs);
}

static void virt_init(MachineState *ms)
{
    HexagonVirtMachineState *vms = HEXAGON_VIRT_MACHINE(ms);
    Error **errp = NULL;
    const hexagon_machine_config *m_cfg = &v68n_1024;

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);

    vms->sys = get_system_memory();

    memory_region_init_ram(&vms->ram, NULL, "ddr.ram", ms->ram_size, errp);
    memory_region_add_subregion(vms->sys, 0x0, &vms->ram);

    if (m_cfg->l2tcm_size) {
        memory_region_init_ram(&vms->tcm, NULL, "tcm.ram", m_cfg->l2tcm_size,
                               errp);
        memory_region_add_subregion(vms->sys, m_cfg->cfgtable.l2tcm_base,
                                    &vms->tcm);
    }

    memory_region_init_rom(&vms->cfgtable, NULL, "config_table.rom",
                           sizeof(m_cfg->cfgtable), errp);
    memory_region_add_subregion(vms->sys, m_cfg->cfgbase, &vms->cfgtable);
    fdt_add_hvx(vms, m_cfg, errp);
    const char *cpu_model = ms->cpu_type;

    if (!cpu_model) {
        cpu_model = HEXAGON_CPU_TYPE_NAME("v73");
    }

    HexagonCPU *cpu_0 = NULL;
    for (int i = 0; i < ms->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(ms->cpu_type));
        qemu_register_reset(do_cpu_reset, cpu);

        if (i == 0) {
            cpu_0 = cpu;
            if (ms->kernel_filename) {
                uint64_t entry = load_kernel(vms);

                qdev_prop_set_uint32(DEVICE(cpu_0), "exec-start-addr", entry);
            }
        }
        qdev_prop_set_uint32(DEVICE(cpu), "l2vic-base-addr", m_cfg->l2vic_base);
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", (i != 0));
        qdev_prop_set_uint32(DEVICE(cpu), "hvx-contexts",
                             m_cfg->cfgtable.ext_contexts);
        qdev_prop_set_uint32(DEVICE(cpu), "num-tlbs",
                             m_cfg->cfgtable.jtlb_size_entries);

        if (!qdev_realize_and_unref(DEVICE(cpu), NULL, errp)) {
            return;
        }
    }
    vms->l2vic = sysbus_create_varargs(
        "l2vic", m_cfg->l2vic_base, qdev_get_gpio_in(DEVICE(cpu_0), 0),
        qdev_get_gpio_in(DEVICE(cpu_0), 1), qdev_get_gpio_in(DEVICE(cpu_0), 2),
        qdev_get_gpio_in(DEVICE(cpu_0), 3), qdev_get_gpio_in(DEVICE(cpu_0), 4),
        qdev_get_gpio_in(DEVICE(cpu_0), 5), qdev_get_gpio_in(DEVICE(cpu_0), 6),
        qdev_get_gpio_in(DEVICE(cpu_0), 7), NULL);

    fdt_add_hvm_pic_node(vms, m_cfg);
    fdt_add_virtio_devices(vms);
    fdt_add_cpu_nodes(vms);
    fdt_add_clocks(vms);
    fdt_add_uart(vms, 0);
    fdt_add_gpt_node(vms);

    hexagon_config_table *config_table =
        (hexagon_config_table *)&m_cfg->cfgtable;

    config_table->l2tcm_base =
        HEXAGON_CFG_ADDR_BASE(m_cfg->cfgtable.l2tcm_base);
    config_table->subsystem_base = HEXAGON_CFG_ADDR_BASE(m_cfg->csr_base);
    config_table->vtcm_base = HEXAGON_CFG_ADDR_BASE(m_cfg->cfgtable.vtcm_base);
    config_table->l2cfg_base =
        HEXAGON_CFG_ADDR_BASE(m_cfg->cfgtable.l2cfg_base);
    config_table->fastl2vic_base =
        HEXAGON_CFG_ADDR_BASE(m_cfg->cfgtable.fastl2vic_base);

    rom_add_blob_fixed_as("config_table.rom", &m_cfg->cfgtable,
                          sizeof(m_cfg->cfgtable), m_cfg->cfgbase,
                          &address_space_memory);


    hexagon_load_fdt(vms);
}


static void virt_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = virt_init;
    mc->default_cpu_type = HEXAGON_CPU_TYPE_NAME("v73");
    mc->default_ram_size = 4 * GiB;
    mc->max_cpus = HEXAGON_MAX_CPUS;
    mc->default_cpus = 6;
    mc->is_default = false;
    mc->default_kernel_irqchip_split = false;
    mc->block_default_type = IF_VIRTIO;
    mc->default_boot_order = NULL;
    mc->no_cdrom = 1;
    mc->numa_mem_supported = false;
    mc->default_nic = "virtio-mmio-bus";
}


static const TypeInfo virt_machine_types[] = { {
    .name = TYPE_HEXAGON_VIRT_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(HexagonVirtMachineState),
    .class_init = virt_class_init,
    .instance_init = virt_instance_init,
} };

DEFINE_TYPES(virt_machine_types)
