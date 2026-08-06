/*
 * Hexagon virt emulation
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hexagon/virt.h"
#include "elf.h"
#include "hw/char/pl011.h"
#include "hw/core/clock.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/register.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"
#include "machine_cfg_v68n_1024.h.inc"
#include "system/address-spaces.h"
#include "system/device_tree.h"
#include "system/reset.h"
#include "system/system.h"
#include <libfdt.h>

enum {
    VIRT_UART0,
    VIRT_MMIO,
    VIRT_FDT,
};

/*
 * Virtio IRQs run from VIRTIO_IRQ_BASE to
 * VIRTIO_IRQ_BASE + VIRTIO_DEV_COUNT - 1
 */
static const int VIRTIO_IRQ_BASE = 16;

static const MemMapEntry base_memmap[] = {
    [VIRT_UART0] = { 0x10000000, 0x00000200 },
    [VIRT_MMIO] = { 0x11000000, 0x00001000 },
    [VIRT_FDT] = { 0x99800000, 0x00400000 },
};


static void create_fdt(HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    void *fdt = create_device_tree(&vms->fdt_size);
    uint8_t rng_seed[32];

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    ms->fdt = fdt;

    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x1);
    qemu_fdt_setprop_string(fdt, "/", "model", "hexagon-virt,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "qcom,sm8150");

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x1);
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static int32_t fdt_add_l2vic(HexagonVirtMachineState *vms,
                             const struct hexagon_machine_config *m_cfg)
{
    MachineState *ms = MACHINE(vms);
    int32_t l2vic_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    char *nodename = g_strdup_printf("/soc/interrupt-controller@%x",
                                     m_cfg->l2vic_base);
    const char compat[] = "qcom,h2-pic\0hvm-pic";

    qemu_fdt_setprop_cell(ms->fdt, "/soc", "interrupt-parent", l2vic_phandle);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 0x1);
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0,
                           m_cfg->l2vic_base, m_cfg->l2vic_size);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", l2vic_phandle);

    g_free(nodename);
    return l2vic_phandle;
}

static void fdt_add_hvx(HexagonVirtMachineState *vms,
                        const struct hexagon_machine_config *m_cfg)
{
    const MachineState *ms = MACHINE(vms);
    uint32_t vtcm_size_bytes = m_cfg->cfgtable.vtcm_size_kb * 1024;
    if (vtcm_size_bytes > 0) {
        qemu_fdt_add_subnode(ms->fdt, "/soc/vtcm");
        qemu_fdt_setprop_string(ms->fdt, "/soc/vtcm", "compatible",
                                "qcom,hexagon_vtcm");

        assert(sizeof(m_cfg->cfgtable.vtcm_base) == sizeof(uint32_t));
        qemu_fdt_setprop_cells(ms->fdt, "/soc/vtcm", "reg", 0,
                               m_cfg->cfgtable.vtcm_base << 16,
                               vtcm_size_bytes);
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

static int32_t fdt_add_clocks(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    int32_t clk_phandle = qemu_fdt_alloc_phandle(ms->fdt);

    qemu_fdt_add_subnode(ms->fdt, "/apb-pclk");
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "clock-frequency", 24000000);
    qemu_fdt_setprop_string(ms->fdt, "/apb-pclk", "clock-output-names",
                            "clk24mhz");
    qemu_fdt_setprop_cell(ms->fdt, "/apb-pclk", "phandle", clk_phandle);

    return clk_phandle;
}

static void fdt_add_uart(const HexagonVirtMachineState *vms, int uart,
                         int32_t clk_phandle)
{
    char *nodename;
    hwaddr base = base_memmap[uart].base;
    hwaddr size = base_memmap[uart].size;
    assert(uart == 0);
    const char compat[] = "arm,pl011\0arm,primecell";
    const char clocknames[] = "uartclk\0apb_pclk";
    MachineState *ms = MACHINE(vms);
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_PL011);
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    qdev_connect_clock_in(dev, "clk", vms->apb_clk);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, base);

    nodename = g_strdup_printf("/pl011@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);

    /* Note that we can't use setprop_string because of the embedded NUL */
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, base, size);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "clocks", clk_phandle,
                           clk_phandle);
    qemu_fdt_setprop(ms->fdt, nodename, "clock-names", clocknames,
                     sizeof(clocknames));

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

static void create_virtio_devices(HexagonVirtMachineState *vms,
                                  int32_t l2vic_phandle)
{
    MachineState *ms = MACHINE(vms);
    hwaddr size = base_memmap[VIRT_MMIO].size;

    for (int i = 0; i < VIRTIO_DEV_COUNT; i++) {
        int irq = VIRTIO_IRQ_BASE + i;
        hwaddr base = base_memmap[VIRT_MMIO].base + i * size;
        char *nodename = g_strdup_printf("/soc/virtio_mmio@%" PRIx64, base);
        DeviceState *dev = qdev_new("virtio-mmio");
        SysBusDevice *s = SYS_BUS_DEVICE(dev);

        object_property_add_child(OBJECT(MACHINE(vms)), "virtio-mmio[*]",
                                  OBJECT(dev));
        sysbus_realize_and_unref(s, &error_fatal);
        sysbus_mmio_map(s, 0, base);
        sysbus_connect_irq(s, 0,
                           qdev_get_gpio_in(vms->parent_obj.l2vic, irq));
        vms->virtio_mmio[i] = dev;

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                "virtio,mmio");
        qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, base, size);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupts", irq);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "interrupt-parent",
                              l2vic_phandle);

        g_free(nodename);
    }
}

void hexagon_load_fdt(const HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    hwaddr fdt_addr = base_memmap[VIRT_FDT].base;
    uint32_t fdtsize = vms->fdt_size;

    g_assert(fdtsize <= base_memmap[VIRT_FDT].size);
    /* copy in the device tree */
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
    if (load_elf_ram_sym(ms->kernel_filename, NULL, NULL, NULL, &entry, NULL,
                         NULL, NULL, 0, EM_HEXAGON, 0, 0, &address_space_memory,
                         false, NULL) > 0) {
        return entry;
    }
    error_report("error loading '%s'", ms->kernel_filename);
    exit(1);
}

static uint64_t load_bios(HexagonVirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    uint64_t bios_addr = 0x0;  /* Load BIOS at reset vector address 0x0 */
    int bios_size;

    bios_size = load_image_targphys(ms->firmware ?: "",
                                    bios_addr, 64 * 1024, NULL);
    if (bios_size < 0) {
        error_report("Could not load BIOS '%s'", ms->firmware ?: "");
        exit(1);
    }

    return bios_addr;  /* Return entry point at address 0x0 */
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
    const struct hexagon_machine_config *m_cfg = &v68n_1024;
    int32_t clk_phandle;
    int32_t l2vic_phandle;

    create_fdt(vms);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);

    vms->sys = get_system_memory();

    /* Create APB clock for peripherals */
    vms->apb_clk = clock_new(OBJECT(ms), "apb-pclk");
    clock_set_hz(vms->apb_clk, 24000000);

    hex_subsys_create(&vms->parent_obj, m_cfg, v68_rev);

    if (m_cfg->l2tcm_size) {
        memory_region_init_ram(&vms->tcm, NULL, "tcm.ram", m_cfg->l2tcm_size,
                               &error_fatal);
        memory_region_add_subregion(vms->sys, m_cfg->cfgtable.l2tcm_base << 16,
                                    &vms->tcm);
    }

    fdt_add_hvx(vms, m_cfg);

    l2vic_phandle = fdt_add_l2vic(vms, m_cfg);
    create_virtio_devices(vms, l2vic_phandle);

    g_autofree HexagonCPU **cpus = g_new(HexagonCPU *, ms->smp.cpus);

    for (int i = 0; i < ms->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(ms->cpu_type));
        qemu_register_reset(do_cpu_reset, cpu);

        if (i == 0) {
            if (ms->kernel_filename) {
                uint64_t entry = load_kernel(vms);
                qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", entry);
            } else if (ms->firmware) {
                uint64_t entry = load_bios(vms);
                qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", entry);
            }
        }
        qdev_prop_set_uint32(DEVICE(cpu), "htid", i);
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", (i != 0));
        hex_subsys_add_cpu(&vms->parent_obj, DEVICE(cpu));
        cpus[i] = cpu;
    }

    hex_subsys_realize_cluster(&vms->parent_obj);

    for (int i = 0; i < ms->smp.cpus; i++) {
        hex_subsys_realize_cpu(&vms->parent_obj, DEVICE(cpus[i]), (i == 0));
    }

    fdt_add_cpu_nodes(vms);
    clk_phandle = fdt_add_clocks(vms);
    fdt_add_uart(vms, VIRT_UART0, clk_phandle);

    hexagon_load_fdt(vms);
}


static void virt_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon Virtual Machine";
    mc->init = virt_init;
    mc->default_cpu_type = HEXAGON_CPU_TYPE_NAME("v68");
    mc->default_ram_size = 4 * GiB;
    mc->max_cpus = 8;
    mc->default_cpus = 8;
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
    .parent = TYPE_HEXAGON_COMMON_MACHINE,
    .instance_size = sizeof(HexagonVirtMachineState),
    .class_init = virt_class_init,
} };

DEFINE_TYPES(virt_machine_types)
