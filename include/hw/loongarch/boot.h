/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for LoongArch boot.
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_BOOT_H
#define HW_LOONGARCH_BOOT_H

struct loongarch_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    uint64_t a0, a1, a2;
};

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info);

#endif /* HW_LOONGARCH_BOOT_H */
