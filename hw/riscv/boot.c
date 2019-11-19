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
#include "qemu-common.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "exec/cpu-defs.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/riscv/boot.h"
#include "elf.h"
#include "sysemu/qtest.h"

#if defined(TARGET_RISCV32)
# define KERNEL_BOOT_ADDRESS 0x80400000
#else
# define KERNEL_BOOT_ADDRESS 0x80200000
#endif

void riscv_find_and_load_firmware(MachineState *machine,
                                  const char *default_machine_firmware,
                                  hwaddr firmware_load_addr)
{
    char *firmware_filename = NULL;

    if (!machine->firmware) {
        /*
         * The user didn't specify -bios.
         * At the moment we default to loading nothing when this hapens.
         * In the future this defaul will change to loading the prebuilt
         * OpenSBI firmware. Let's warn the user and then continue.
        */
        if (!qtest_enabled()) {
            warn_report("No -bios option specified. Not loading a firmware.");
            warn_report("This default will change in a future QEMU release. " \
                        "Please use the -bios option to avoid breakages when "\
                        "this happens.");
            warn_report("See QEMU's deprecation documentation for details.");
        }
        return;
    }

    if (!strcmp(machine->firmware, "default")) {
        /*
         * The user has specified "-bios default". That means we are going to
         * load the OpenSBI binary included in the QEMU source.
         *
         * We can't load the binary by default as it will break existing users
         * as users are already loading their own firmware.
         *
         * Let's try to get everyone to specify the -bios option at all times,
         * so then in the future we can make "-bios default" the default option
         * if no -bios option is set without breaking anything.
         */
        firmware_filename = riscv_find_firmware(default_machine_firmware);
    } else if (strcmp(machine->firmware, "none")) {
        firmware_filename = riscv_find_firmware(machine->firmware);
    }

    if (firmware_filename) {
        /* If not "none" load the firmware */
        riscv_load_firmware(firmware_filename, firmware_load_addr);
        g_free(firmware_filename);
    }
}

char *riscv_find_firmware(const char *firmware_filename)
{
    char *filename;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware_filename);
    if (filename == NULL) {
        error_report("Unable to load the RISC-V firmware \"%s\"",
                     firmware_filename);
        exit(1);
    }

    return filename;
}

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

target_ulong riscv_load_kernel(const char *kernel_filename, symbol_fn_t sym_cb)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf_ram_sym(kernel_filename, NULL, NULL, NULL,
                         &kernel_entry, NULL, &kernel_high, 0,
                         EM_RISCV, 1, 0, NULL, true, sym_cb) > 0) {
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
