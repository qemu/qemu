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
#include "hw/riscv/boot_opensbi.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define KERNEL_BOOT_ADDRESS 0x80400000
#define fw_dynamic_info_data(__val)     cpu_to_le32(__val)
#else
# define KERNEL_BOOT_ADDRESS 0x80200000
#define fw_dynamic_info_data(__val)     cpu_to_le64(__val)
#endif

void riscv_find_and_load_firmware(MachineState *machine,
                                  const char *default_machine_firmware,
                                  hwaddr firmware_load_addr,
                                  symbol_fn_t sym_cb)
{
    char *firmware_filename = NULL;

    if ((!machine->firmware) || (!strcmp(machine->firmware, "default"))) {
        /*
         * The user didn't specify -bios, or has specified "-bios default".
         * That means we are going to load the OpenSBI binary included in
         * the QEMU source.
         */
        firmware_filename = riscv_find_firmware(default_machine_firmware);
    } else if (strcmp(machine->firmware, "none")) {
        firmware_filename = riscv_find_firmware(machine->firmware);
    }

    if (firmware_filename) {
        /* If not "none" load the firmware */
        riscv_load_firmware(firmware_filename, firmware_load_addr, sym_cb);
        g_free(firmware_filename);
    }
}

char *riscv_find_firmware(const char *firmware_filename)
{
    char *filename;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware_filename);
    if (filename == NULL) {
        if (!qtest_enabled()) {
            /*
             * We only ship plain binary bios images in the QEMU source.
             * With Spike machine that uses ELF images as the default bios,
             * running QEMU test will complain hence let's suppress the error
             * report for QEMU testing.
             */
            error_report("Unable to load the RISC-V firmware \"%s\"",
                         firmware_filename);
            exit(1);
        }
    }

    return filename;
}

target_ulong riscv_load_firmware(const char *firmware_filename,
                                 hwaddr firmware_load_addr,
                                 symbol_fn_t sym_cb)
{
    uint64_t firmware_entry, firmware_start, firmware_end;

    if (load_elf_ram_sym(firmware_filename, NULL, NULL, NULL,
                         &firmware_entry, &firmware_start, &firmware_end, NULL,
                         0, EM_RISCV, 1, 0, NULL, true, sym_cb) > 0) {
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
                         &kernel_entry, NULL, &kernel_high, NULL, 0,
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

uint32_t riscv_load_fdt(hwaddr dram_base, uint64_t mem_size, void *fdt)
{
    uint32_t temp, fdt_addr;
    hwaddr dram_end = dram_base + mem_size;
    int fdtsize = fdt_totalsize(fdt);

    if (fdtsize <= 0) {
        error_report("invalid device-tree");
        exit(1);
    }

    /*
     * We should put fdt as far as possible to avoid kernel/initrd overwriting
     * its content. But it should be addressable by 32 bit system as well.
     * Thus, put it at an aligned address that less than fdt size from end of
     * dram or 4GB whichever is lesser.
     */
    temp = MIN(dram_end, 4096 * MiB);
    fdt_addr = QEMU_ALIGN_DOWN(temp - fdtsize, 2 * MiB);

    fdt_pack(fdt);
    /* copy in the device tree */
    qemu_fdt_dumpdtb(fdt, fdtsize);

    rom_add_blob_fixed_as("fdt", fdt, fdtsize, fdt_addr,
                          &address_space_memory);

    return fdt_addr;
}

void riscv_rom_copy_firmware_info(hwaddr rom_base, hwaddr rom_size,
                              uint32_t reset_vec_size, uint64_t kernel_entry)
{
    struct fw_dynamic_info dinfo;
    size_t dinfo_len;

    dinfo.magic = fw_dynamic_info_data(FW_DYNAMIC_INFO_MAGIC_VALUE);
    dinfo.version = fw_dynamic_info_data(FW_DYNAMIC_INFO_VERSION);
    dinfo.next_mode = fw_dynamic_info_data(FW_DYNAMIC_INFO_NEXT_MODE_S);
    dinfo.next_addr = fw_dynamic_info_data(kernel_entry);
    dinfo.options = 0;
    dinfo.boot_hart = 0;
    dinfo_len = sizeof(dinfo);

    /**
     * copy the dynamic firmware info. This information is specific to
     * OpenSBI but doesn't break any other firmware as long as they don't
     * expect any certain value in "a2" register.
     */
    if (dinfo_len > (rom_size - reset_vec_size)) {
        error_report("not enough space to store dynamic firmware info");
        exit(1);
    }

    rom_add_blob_fixed_as("mrom.finfo", &dinfo, dinfo_len,
                           rom_base + reset_vec_size,
                           &address_space_memory);
}

void riscv_setup_rom_reset_vec(hwaddr start_addr, hwaddr rom_base,
                               hwaddr rom_size, uint64_t kernel_entry,
                               uint32_t fdt_load_addr, void *fdt)
{
    int i;
    uint32_t start_addr_hi32 = 0x00000000;

    #if defined(TARGET_RISCV64)
    start_addr_hi32 = start_addr >> 32;
    #endif
    /* reset vector */
    uint32_t reset_vec[10] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(fw_dyn) */
        0x02828613,                  /*     addi   a2, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0202a583,                  /*     lw     a1, 32(t0) */
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0202b583,                  /*     ld     a1, 32(t0) */
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        start_addr,                  /* start: .dword */
        start_addr_hi32,
        fdt_load_addr,               /* fdt_laddr: .dword */
        0x00000000,
                                     /* fw_dyn: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < ARRAY_SIZE(reset_vec); i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          rom_base, &address_space_memory);
    riscv_rom_copy_firmware_info(rom_base, rom_size, sizeof(reset_vec),
                                 kernel_entry);

    return;
}
