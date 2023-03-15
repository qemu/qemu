/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU OpenRISC boot helpers.
 *
 * (c) 2022 Stafford Horne <shorne@gmail.com>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-defs.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/openrisc/boot.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"

#include <libfdt.h>

#define KERNEL_LOAD_ADDR 0x100

hwaddr openrisc_load_kernel(ram_addr_t ram_size,
                            const char *kernel_filename,
                            uint32_t *bootstrap_pc)
{
    long kernel_size;
    uint64_t elf_entry;
    uint64_t high_addr;
    hwaddr entry;

    if (kernel_filename && !qtest_enabled()) {
        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, &high_addr, NULL, 1,
                               EM_OPENRISC, 1, 0);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename,
                                      &entry, NULL, NULL, NULL, NULL);
            high_addr = entry + kernel_size;
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
            high_addr = KERNEL_LOAD_ADDR + kernel_size;
        }

        if (entry <= 0) {
            entry = KERNEL_LOAD_ADDR;
        }

        if (kernel_size < 0) {
            error_report("couldn't load the kernel '%s'", kernel_filename);
            exit(1);
        }
        *bootstrap_pc = entry;

        return high_addr;
    }
    return 0;
}

hwaddr openrisc_load_initrd(void *fdt, const char *filename,
                            hwaddr load_start, uint64_t mem_size)
{
    int size;
    hwaddr start;

    /* We put the initrd right after the kernel; page aligned. */
    start = TARGET_PAGE_ALIGN(load_start);

    size = load_ramdisk(filename, start, mem_size - start);
    if (size < 0) {
        size = load_image_targphys(filename, start, mem_size - start);
        if (size < 0) {
            error_report("could not load ramdisk '%s'", filename);
            exit(1);
        }
    }

    if (fdt) {
        qemu_fdt_setprop_cell(fdt, "/chosen",
                              "linux,initrd-start", start);
        qemu_fdt_setprop_cell(fdt, "/chosen",
                              "linux,initrd-end", start + size);
    }

    return start + size;
}

uint32_t openrisc_load_fdt(void *fdt, hwaddr load_start,
                           uint64_t mem_size)
{
    uint32_t fdt_addr;
    int ret;
    int fdtsize = fdt_totalsize(fdt);

    if (fdtsize <= 0) {
        error_report("invalid device-tree");
        exit(1);
    }

    /* We put fdt right after the kernel and/or initrd. */
    fdt_addr = TARGET_PAGE_ALIGN(load_start);

    ret = fdt_pack(fdt);
    /* Should only fail if we've built a corrupted tree */
    g_assert(ret == 0);
    /* copy in the device tree */
    qemu_fdt_dumpdtb(fdt, fdtsize);

    rom_add_blob_fixed_as("fdt", fdt, fdtsize, fdt_addr,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
                        rom_ptr_for_as(&address_space_memory, fdt_addr, fdtsize));

    return fdt_addr;
}
