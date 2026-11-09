/*
 * DSP subsystem machines: the board layer shared by the SoC models.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hex-board.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/hexagon/qcom_dsp.h"
#include "hw/misc/unimp.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/device_tree.h"

enum {
    QCOM_DSP_UART0,
    QCOM_DSP_VIRTIO,
    QCOM_DSP_GPT,
    QCOM_DSP_BOOT,
};

static const MemMapEntry qcom_dsp_memmap[] = {
    [QCOM_DSP_UART0] = { 0x37000000, 0x00001000 },
    [QCOM_DSP_VIRTIO] = { 0x37010000, 0x00001000 },
    /* Not a device: the window the kernel's H2 timer driver binds to. */
    [QCOM_DSP_GPT] = { 0xab000000, 0x00001000 },
    [QCOM_DSP_BOOT] = { 0x99c00000, 0x00000200 },
};

static const int QCOM_DSP_UART0_IRQ = 15;
/* Virtio IRQs run from QCOM_DSP_VIRTIO_IRQ to QCOM_DSP_VIRTIO_IRQ + 7 */
static const int QCOM_DSP_VIRTIO_IRQ = 96;
static const int QCOM_DSP_GPT_IRQ = 12;

#define QCOM_DSP_L2_ECC_SIZE 0x1000

static const QcomDspMachineInfo *qcom_dsp_info(MachineState *ms)
{
    return QCOM_DSP_MACHINE_GET_CLASS(ms)->info;
}

void qcom_dsp_add_regions(MachineState *ms, const QcomDspRegion *regions,
                          unsigned int count)
{
    const char *machine = MACHINE_GET_CLASS(ms)->name;

    for (unsigned int i = 0; i < count; i++) {
        g_autofree char *name = g_strdup_printf("%s.%s@%" HWADDR_PRIx, machine,
                                                regions[i].name,
                                                regions[i].base);

        create_unimplemented_device(name, regions[i].base,
                                    regions[i].size ? regions[i].size : 0x1000);
    }
}

void qcom_dsp_add_device(const char *type, hwaddr base)
{
    DeviceState *dev = qdev_new(type);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
}

void qcom_soc_add_qup_geni_ports(const char *soc, hwaddr base,
                                 unsigned int ports, hwaddr size)
{
    for (unsigned int i = 0; i < ports; i++) {
        g_autofree char *name = g_strdup_printf("%s.qup-geni@%" HWADDR_PRIx,
                                                soc, base + i * size);

        create_unimplemented_device(name, base + i * size, size);
    }
}

/*
 * The firmware programs the L2 ECC registers and polls them until what it
 * wrote reads back, so they need to hold state.
 */
static void qcom_dsp_add_l2_ecc_regs(QcomDspMachineState *qms,
                                     const QcomDspMachineInfo *info)
{
    hwaddr base = (hwaddr)info->cfg->cfgtable.eccreg_base << 16;

    if (!base) {
        return;
    }
    memory_region_init_ram(&qms->l2_ecc, NULL, "l2-ecc-regs",
                           QCOM_DSP_L2_ECC_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, &qms->l2_ecc);
}

static void qcom_dsp_create_fdt(QcomDspMachineState *qms)
{
    MachineState *ms = MACHINE(qms);
    g_autofree char *compatible = g_strdup_printf("qcom,%s",
                                             MACHINE_GET_CLASS(ms)->name);

    hex_board_create_fdt(ms, MACHINE_GET_CLASS(ms)->desc, compatible,
                         &qms->fdt_size);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
}

static void qcom_dsp_fdt_add_memory(MachineState *ms)
{
    hwaddr kernel_addr = qcom_dsp_info(ms)->kernel_addr;
    hwaddr ram_end = qcom_dsp_info(ms)->cfg->ddr_base + ms->ram_size;

    hex_board_fdt_add_memory(ms, kernel_addr,
                             MIN(ram_end - kernel_addr,
                                 HEX_BOARD_MAX_KERNEL_RAM));
}

/* Reject boot options that do not make sense together, and find -bios. */
static void qcom_dsp_check_boot_options(QcomDspMachineState *qms)
{
    MachineState *ms = MACHINE(qms);

    if (ms->firmware) {
        qms->firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, ms->firmware);
        if (!qms->firmware_path) {
            error_report("Could not find firmware '%s'", ms->firmware);
            exit(1);
        }
    }

    hex_board_check_initrd(ms, qms->firmware_path);
}

/* Load what -bios and -kernel name and return where CPU 0 starts, if known. */
static bool qcom_dsp_load_images(QcomDspMachineState *qms, uint64_t *start)
{
    MachineState *ms = MACHINE(qms);
    const QcomDspMachineInfo *info = qcom_dsp_info(ms);

    if (ms->kernel_filename && qms->firmware_path) {
        *start = hex_board_load_vm_guest(ms, qms->firmware_path,
                                         info->cfg->ddr_base, info->kernel_addr,
                                         qcom_dsp_memmap[QCOM_DSP_BOOT].base,
                                         qms->fdt_size, &qms->fdt_addr);
        return true;
    }
    if (ms->kernel_filename) {
        *start = hex_board_load_elf(ms->kernel_filename, NULL, NULL, NULL);
        return true;
    }
    if (qms->firmware_path) {
        *start = hex_board_load_elf(qms->firmware_path, NULL, NULL, NULL);
        return true;
    }
    return false;
}

static void qcom_dsp_init(MachineState *ms)
{
    QcomDspMachineState *qms = QCOM_DSP_MACHINE(ms);
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(ms);
    const QcomDspMachineInfo *info = qcom_dsp_info(ms);
    int32_t l2vic_phandle;
    uint64_t start = 0;
    bool have_start;

    qcom_dsp_check_boot_options(qms);
    qcom_dsp_create_fdt(qms);
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);

    hex_subsys_create(hms, info->cfg, info->rev);
    hex_board_create_tcm(info->cfg, &qms->tcm);

    hex_board_fdt_add_hvx(ms, info->cfg);
    l2vic_phandle = hex_board_fdt_add_l2vic(ms, info->cfg);
    hex_board_fdt_add_vm_timer(ms, qcom_dsp_memmap[QCOM_DSP_GPT].base,
                               qcom_dsp_memmap[QCOM_DSP_GPT].size,
                               QCOM_DSP_GPT_IRQ);
    /* loadlinux gives the kernel a virtual CPU for every thread that runs. */
    hex_board_fdt_add_cpus(ms);
    if (ms->kernel_filename && qms->firmware_path) {
        qcom_dsp_fdt_add_memory(ms);
    }
    hex_board_create_uart(ms, qcom_dsp_memmap[QCOM_DSP_UART0].base,
                          qcom_dsp_memmap[QCOM_DSP_UART0].size,
                          QCOM_DSP_UART0_IRQ, l2vic_phandle);
    hex_board_create_virtio(ms, qcom_dsp_memmap[QCOM_DSP_VIRTIO].base,
                            qcom_dsp_memmap[QCOM_DSP_VIRTIO].size,
                            QCOM_DSP_VIRTIO_IRQ, QCOM_DSP_VIRTIO_COUNT,
                            l2vic_phandle);
    qcom_dsp_add_l2_ecc_regs(qms, info);
    if (info->init_devices) {
        info->init_devices(ms);
    }

    if (ms->dtb) {
        g_free(ms->fdt);
        ms->fdt = load_device_tree(ms->dtb, &qms->fdt_size);
        if (!ms->fdt) {
            error_report("could not load device tree '%s'", ms->dtb);
            exit(1);
        }
        if (ms->kernel_cmdline && *ms->kernel_cmdline) {
            qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs",
                                    ms->kernel_cmdline);
        }
    }

    have_start = qcom_dsp_load_images(qms, &start);
    hex_board_create_cpus(ms, have_start, start);

    if (ms->kernel_filename && qms->firmware_path) {
        hex_board_load_fdt(ms, qms->fdt_size, qms->fdt_addr);
    }
}

static void qcom_dsp_base_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = qcom_dsp_init;
    mc->default_ram_size = 1 * GiB;
    mc->is_default = false;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = true;
    mc->no_floppy = true;
    mc->no_parallel = true;
    mc->numa_mem_supported = false;
}

void qcom_dsp_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    QcomDspMachineClass *qc = QCOM_DSP_MACHINE_CLASS(oc);
    const QcomDspMachineInfo *info = data;

    qc->info = info;
    mc->desc = info->desc;
    mc->default_cpu_type = info->cpu_type;
    mc->default_cpus = info->cpus;
    mc->max_cpus = info->cpus;
}

static const TypeInfo qcom_dsp_types[] = {
    {
        .name = TYPE_QCOM_DSP_MACHINE,
        .parent = TYPE_HEXAGON_COMMON_MACHINE,
        .instance_size = sizeof(QcomDspMachineState),
        .class_size = sizeof(QcomDspMachineClass),
        .class_init = qcom_dsp_base_class_init,
        .abstract = true,
    },
};

DEFINE_TYPES(qcom_dsp_types)
