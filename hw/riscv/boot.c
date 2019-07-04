/*
 * QEMU RISC-V Boot Helper
 *
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2019 Alistair Francis <alistair.francis@wdc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "exec/cpu-defs.h"
#include "hw/loader.h"
#include "hw/riscv/boot.h"
#include "hw/boards.h"
#include "elf.h"

#if defined(TARGET_RISCV32)
# define KERNEL_BOOT_ADDRESS 0x80400000
#else
# define KERNEL_BOOT_ADDRESS 0x80200000
#endif

target_ulong riscv_load_firmware(const char *firmware_filename,
                                 hwaddr firmware_load_addr)
{
    uint64_t firmware_entry, firmware_start, firmware_end;

    if (load_elf(firmware_filename, NULL, NULL, NULL, &firmware_entry,
                 &firmware_start, &firmware_end, 0, EM_RISCV, 1, 0) > 0) {
        return firmware_entry;
    }

    if (load_image_targphys_as(firmware_filename, firmware_load_addr,
                               ram_size, NULL) > 0) {
        return firmware_load_addr;
    }

    error_report("could not load firmware '%s'", firmware_filename);
    exit(1);
}

target_ulong riscv_load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf(kernel_filename, NULL, NULL, NULL,
                 &kernel_entry, NULL, &kernel_high, 0, EM_RISCV, 1, 0) > 0) {
        return kernel_entry;
    }

    if (load_uimage_as(kernel_filename, &kernel_entry, NULL, NULL,
                       NULL, NULL, NULL) > 0) {
        return kernel_entry;
    }

    if (load_image_targphys_as(kernel_filename, KERNEL_BOOT_ADDRESS,
                               ram_size, NULL) > 0) {
        return KERNEL_BOOT_ADDRESS;
    }

    error_report("could not load kernel '%s'", kernel_filename);
    exit(1);
}

hwaddr riscv_load_initrd(const char *filename, uint64_t mem_size,
                         uint64_t kernel_entry, hwaddr *start)
{
    int size;

    /*
     * We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM we must avoid the initrd being so far up in RAM
     * that it is outside lowmem and inaccessible to the kernel.
     * So for boards with less  than 256MB of RAM we put the initrd
     * halfway into RAM, and for boards with 256MB of RAM or more we put
     * the initrd at 128MB.
     */
    *start = kernel_entry + MIN(mem_size / 2, 128 * MiB);

    size = load_ramdisk(filename, *start, mem_size - *start);
    if (size == -1) {
        size = load_image_targphys(filename, *start, mem_size - *start);
        if (size == -1) {
            error_report("could not load ramdisk '%s'", filename);
            exit(1);
        }
    }

    return *start + size;
}
