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

#ifndef RISCV_BOOT_H
#define RISCV_BOOT_H

#include "exec/cpu-defs.h"
#include "hw/loader.h"
#include "hw/riscv/riscv_hart.h"

#define RISCV32_BIOS_BIN    "opensbi-riscv32-generic-fw_dynamic.bin"
#define RISCV64_BIOS_BIN    "opensbi-riscv64-generic-fw_dynamic.bin"

bool riscv_is_32bit(RISCVHartArrayState *harts);

char *riscv_plic_hart_config_string(int hart_count);

target_ulong riscv_calc_kernel_start_addr(RISCVHartArrayState *harts,
                                          target_ulong firmware_end_addr);
target_ulong riscv_find_and_load_firmware(MachineState *machine,
                                          const char *default_machine_firmware,
                                          hwaddr firmware_load_addr,
                                          symbol_fn_t sym_cb);
char *riscv_find_firmware(const char *firmware_filename);
target_ulong riscv_load_firmware(const char *firmware_filename,
                                 hwaddr firmware_load_addr,
                                 symbol_fn_t sym_cb);
target_ulong riscv_load_kernel(const char *kernel_filename,
                               target_ulong firmware_end_addr,
                               symbol_fn_t sym_cb);
hwaddr riscv_load_initrd(const char *filename, uint64_t mem_size,
                         uint64_t kernel_entry, hwaddr *start);
uint32_t riscv_load_fdt(hwaddr dram_start, uint64_t dram_size, void *fdt);
void riscv_setup_rom_reset_vec(MachineState *machine, RISCVHartArrayState *harts,
                               hwaddr saddr,
                               hwaddr rom_base, hwaddr rom_size,
                               uint64_t kernel_entry,
                               uint32_t fdt_load_addr, void *fdt);
void riscv_rom_copy_firmware_info(MachineState *machine, hwaddr rom_base,
                                  hwaddr rom_size,
                                  uint32_t reset_vec_size,
                                  uint64_t kernel_entry);
void riscv_setup_direct_kernel(hwaddr kernel_addr, hwaddr fdt_addr);

#endif /* RISCV_BOOT_H */
