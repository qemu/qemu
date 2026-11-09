/*
 * Hexagon virt emulation
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "elf.h"
#include "hw/hexagon/virt.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hex-board.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/core/loader.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "qemu/units.h"
#include "machine_cfg_v68n_1024.h.inc"
#include "system/device_tree.h"

enum {
    VIRT_UART0,
    VIRT_MMIO,
    VIRT_FDT,
    VIRT_BOOT,
};

/*
 * Virtio IRQs run from VIRTIO_IRQ_BASE to
 * VIRTIO_IRQ_BASE + VIRTIO_DEV_COUNT - 1
 */
static const int VIRTIO_IRQ_BASE = 16;
static const int VIRT_UART0_IRQ = 15;

static const MemMapEntry base_memmap[] = {
    [VIRT_UART0] = { 0x10000000, 0x00001000 },
    [VIRT_MMIO] = { 0x11000000, 0x00001000 },
    [VIRT_FDT] = { 0x99800000, 0x00400000 },
    [VIRT_BOOT] = { 0x99c00000, 0x00000200 },
};


static uint64_t load_kernel(const HexagonVirtMachineState *vms)
{
    return hex_board_load_elf(MACHINE(vms)->kernel_filename, NULL, NULL, NULL);
}

static bool is_elf(const char *filename)
{
    uint8_t magic[SELFMAG];
    int fd = qemu_open_old(filename, O_RDONLY | O_BINARY);
    bool elf;

    if (fd < 0) {
        return false;
    }
    elf = read(fd, magic, sizeof(magic)) == sizeof(magic) &&
          memcmp(magic, ELFMAG, SELFMAG) == 0;
    qemu_close(fd);
    return elf;
}

/*
 * An ELF firmware starts at its entry point.  Any other firmware is a raw
 * image that starts at the reset vector, address zero, where it is loaded.
 */
static uint64_t load_bios(MachineState *ms, const char *filename)
{
    if (is_elf(filename)) {
        return hex_board_load_elf(filename, NULL, NULL, NULL);
    }
    if (load_image_targphys(filename, 0, ms->ram_size, NULL) < 0) {
        error_report("Could not load BIOS '%s'", filename);
        exit(1);
    }
    return 0;
}

static void virt_init(MachineState *ms)
{
    HexagonVirtMachineState *vms = HEXAGON_VIRT_MACHINE(ms);
    const struct hexagon_machine_config *m_cfg = &v68n_1024;
    int32_t l2vic_phandle;
    uint64_t start = 0;
    bool have_start = false;
    hwaddr fdt_addr = base_memmap[VIRT_FDT].base;
    g_autofree char *firmware = NULL;
    bool vm_guest;

    if (ms->firmware) {
        firmware = qemu_find_file(QEMU_FILE_TYPE_BIOS, ms->firmware);
        if (!firmware) {
            error_report("Could not find BIOS '%s'", ms->firmware);
            exit(1);
        }
    }
    hex_board_check_initrd(ms, firmware);
    vm_guest = ms->kernel_filename && firmware;

    hex_board_create_fdt(ms, "hexagon-virt,qemu", "qcom,sm8150",
                         &vms->fdt_size);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_string(ms->fdt, "/chosen", "bootargs", ms->kernel_cmdline);

    hex_subsys_create(&vms->parent_obj, m_cfg, v68_rev);
    hex_board_create_tcm(m_cfg, &vms->tcm);
    hex_board_fdt_add_hvx(ms, m_cfg);

    l2vic_phandle = hex_board_fdt_add_l2vic(ms, m_cfg);
    hex_board_create_virtio(ms, base_memmap[VIRT_MMIO].base,
                            base_memmap[VIRT_MMIO].size, VIRTIO_IRQ_BASE,
                            VIRTIO_DEV_COUNT, l2vic_phandle);

    if (vm_guest) {
        start = hex_board_load_vm_guest(ms, firmware, 0, 0,
                                        base_memmap[VIRT_BOOT].base,
                                        vms->fdt_size, &fdt_addr);
        have_start = true;
    } else if (ms->kernel_filename) {
        start = load_kernel(vms);
        have_start = true;
    } else if (firmware) {
        start = load_bios(ms, firmware);
        have_start = true;
    }
    hex_board_create_cpus(ms, have_start, start);

    hex_board_fdt_add_cpus(ms);
    hex_board_create_uart(ms, base_memmap[VIRT_UART0].base,
                          base_memmap[VIRT_UART0].size, VIRT_UART0_IRQ,
                          l2vic_phandle);

    hex_board_load_fdt(ms, vms->fdt_size, fdt_addr);
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
    .is_available = target_hexagon,
} };

DEFINE_TYPES(virt_machine_types)
