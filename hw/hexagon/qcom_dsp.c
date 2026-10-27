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

#define QCOM_DSP_MAX_KERNEL_RAM (896 * MiB)

static const uint32_t bootloader[] = {
    /* Load fdt_base_low value into r0: */
    0x099c4000, /* { immext(#0x99c00000) */
    0x7800c606, /*   r6 = ##-0x662fffd0 } */
    0x9186c000, /* { r0 = memw(r6+#0x0) } */

    /* Load fdt_base_high value into r1: */
    0x099c4000, /* { immext(#0x99c00000) */
    0x7800c586, /*   r6 = ##-0x662fffd4 } */
    0x9186c001, /* { r1 = memw(r6+#0x0) } */

    /* Load next_stage_entry value into r7: */
    0x099c4000, /* { immext(#0x99c00000) */
    0x7800c687, /*   r7 = ##-0x662fffcc } */
    0x9187c007, /* { r7 = memw(r7+#0x0) } */

    /* Jump to next_stage_entry, r1:0 now contains fdt_base: */
    0x5287c000, /* { jumpr r7 } */
    0x0, /* Invalid packet */
    0x0, /* Pad for fdt_base_high */
    0x0, /* Pad for fdt_base_low */
    0x0, /* Pad for next_stage_entry */
};

enum {
    FDT_HI = 11,
    FDT_LO,
    ENTRY_ADDR,
};

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

/*
 * The H2 hypervisor provides the guest timer through hypercalls, so this
 * node only tells the kernel's timer driver which interrupt to expect.
 */
static void qcom_dsp_fdt_add_gpt(MachineState *ms)
{
    static const char compat[] = "qcom,h2-timer\0hvm-timer";
    g_autofree char *name = g_strdup_printf("/soc/gpt@%" PRIx64,
                                            qcom_dsp_memmap[QCOM_DSP_GPT].base);

    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop(ms->fdt, name, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", QCOM_DSP_GPT_IRQ, 0);
    qemu_fdt_setprop_cells(ms->fdt, name, "reg", 0x0,
                           qcom_dsp_memmap[QCOM_DSP_GPT].base,
                           qcom_dsp_memmap[QCOM_DSP_GPT].size);
}

static void qcom_dsp_fdt_add_memory(MachineState *ms)
{
    hwaddr kernel_addr = qcom_dsp_info(ms)->kernel_addr;
    hwaddr ram_end = qcom_dsp_info(ms)->cfg->ddr_base + ms->ram_size;
    hwaddr size = MIN(ram_end - kernel_addr, QCOM_DSP_MAX_KERNEL_RAM);
    g_autofree char *nodename = g_strdup_printf("/memory@%" HWADDR_PRIx,
                                                kernel_addr);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "memory");
    qemu_fdt_setprop_cells(ms->fdt, nodename, "reg", 0, kernel_addr,
                           size);
}

static uint64_t qcom_dsp_kernel_translate(void *opaque, uint64_t addr)
{
    return addr + qcom_dsp_info(MACHINE(opaque))->kernel_addr;
}

static uint64_t qcom_dsp_load_kernel(MachineState *ms, bool through_firmware,
                                     hwaddr *image_high)
{
    return hex_board_load_elf(ms->kernel_filename,
                              through_firmware ? qcom_dsp_kernel_translate
                                               : NULL,
                              ms, image_high);
}

static uint64_t qcom_dsp_load_firmware(QcomDspMachineState *qms)
{
    return hex_board_load_elf(qms->firmware_path, NULL, NULL, NULL);
}

/*
 * Place the FDT after the kernel image, inside the RAM window the kernel
 * maps, and the initrd after that.
 */
static void qcom_dsp_load_initrd(QcomDspMachineState *qms)
{
    MachineState *ms = MACHINE(qms);
    hwaddr start = qms->fdt_addr + 4 * MiB;
    hwaddr ram_end = qcom_dsp_info(ms)->cfg->ddr_base + ms->ram_size;
    hwaddr available;
    ssize_t size;

    available = qms->fdt_addr < ram_end ? ram_end - qms->fdt_addr : 0;
    if (qms->fdt_size > available) {
        error_report("no room for the FDT (size 0x%x): only 0x%" HWADDR_PRIx
                     " bytes available at 0x%" HWADDR_PRIx,
                     qms->fdt_size, available, qms->fdt_addr);
        exit(1);
    }

    if (!ms->initrd_filename) {
        return;
    }

    if (start >= ram_end) {
        error_report("no room for the initrd: RAM ends at 0x%" HWADDR_PRIx
                     " but the initrd would start at 0x%" HWADDR_PRIx,
                     ram_end, start);
        exit(1);
    }

    size = load_image_targphys_as(ms->initrd_filename, start, ram_end - start,
                                  &address_space_memory, &error_fatal);

    qemu_fdt_setprop_u64(ms->fdt, "/chosen", "linux,initrd-start", start);
    qemu_fdt_setprop_u64(ms->fdt, "/chosen", "linux,initrd-end", start + size);
}

/*
 * A boot stub that passes the FDT address to the firmware in r1:r0, then
 * jumps to the firmware's entry point.
 */
static uint64_t qcom_dsp_setup_boot_stub(QcomDspMachineState *qms,
                                         uint64_t firmware_entry)
{
    hwaddr bootl_base = qcom_dsp_memmap[QCOM_DSP_BOOT].base;
    uint32_t stub[ARRAY_SIZE(bootloader)];

    for (size_t i = 0; i < ARRAY_SIZE(stub); i++) {
        stub[i] = cpu_to_le32(bootloader[i]);
    }
    stub[FDT_LO] = cpu_to_le32(extract64(qms->fdt_addr, 0, 32));
    stub[FDT_HI] = cpu_to_le32(extract64(qms->fdt_addr, 32, 32));
    stub[ENTRY_ADDR] = cpu_to_le32(extract64(firmware_entry, 0, 32));

    g_assert(sizeof(stub) <= qcom_dsp_memmap[QCOM_DSP_BOOT].size);
    rom_add_blob_fixed_as("bootloader", stub, sizeof(stub), bootl_base,
                          &address_space_memory);

    return bootl_base;
}

/* Reject boot options that do not make sense together, and find -bios. */
static void qcom_dsp_check_boot_options(QcomDspMachineState *qms)
{
    MachineState *ms = MACHINE(qms);
    hwaddr ddr_base = qcom_dsp_info(ms)->cfg->ddr_base;
    hwaddr kernel_addr = qcom_dsp_info(ms)->kernel_addr;

    if (ms->firmware) {
        qms->firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, ms->firmware);
        if (!qms->firmware_path) {
            error_report("Could not find firmware '%s'", ms->firmware);
            exit(1);
        }
    }

    if (ms->initrd_filename && !(ms->kernel_filename && qms->firmware_path)) {
        error_report("-initrd needs both -kernel and the H2 loadlinux "
                     "firmware given with -bios");
        exit(1);
    }

    if (ms->kernel_filename && qms->firmware_path &&
        (ddr_base > qcom_dsp_memmap[QCOM_DSP_BOOT].base ||
         ddr_base + ms->ram_size <= kernel_addr)) {
        error_report("RAM (%" PRIu64 " MiB at 0x%" HWADDR_PRIx ") is too "
                     "small: the kernel is loaded at 0x%" HWADDR_PRIx,
                     ms->ram_size / MiB, ddr_base, kernel_addr);
        exit(1);
    }
}

/* Load what -bios and -kernel name and return where CPU 0 starts, if known. */
static bool qcom_dsp_load_images(QcomDspMachineState *qms, uint64_t *start)
{
    MachineState *ms = MACHINE(qms);
    hwaddr image_high = 0;

    if (ms->kernel_filename && qms->firmware_path) {
        uint64_t firmware_entry = qcom_dsp_load_firmware(qms);

        qcom_dsp_load_kernel(ms, true, &image_high);
        qms->fdt_addr = QEMU_ALIGN_UP(image_high + 16 * MiB, 4 * MiB);
        qcom_dsp_load_initrd(qms);
        *start = qcom_dsp_setup_boot_stub(qms, firmware_entry);
        return true;
    }
    if (ms->kernel_filename) {
        *start = qcom_dsp_load_kernel(ms, false, &image_high);
        return true;
    }
    if (qms->firmware_path) {
        *start = qcom_dsp_load_firmware(qms);
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
    qcom_dsp_fdt_add_gpt(ms);
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
