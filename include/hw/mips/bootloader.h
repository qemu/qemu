/*
 * Utility for QEMU MIPS to generate it's simple bootloader
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_BOOTLOADER_H
#define HW_MIPS_BOOTLOADER_H

#include "exec/cpu-defs.h"

void bl_gen_jump_to(void **ptr, target_ulong jump_addr);
void bl_gen_jump_kernel(void **ptr,
                        bool set_sp, target_ulong sp,
                        bool set_a0, target_ulong a0,
                        bool set_a1, target_ulong a1,
                        bool set_a2, target_ulong a2,
                        bool set_a3, target_ulong a3,
                        target_ulong kernel_addr);
void bl_gen_write_ulong(void **ptr, target_ulong addr, target_ulong val);
void bl_gen_write_u32(void **ptr, target_ulong addr, uint32_t val);
void bl_gen_write_u64(void **ptr, target_ulong addr, uint64_t val);

#endif
