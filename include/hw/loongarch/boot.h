/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for LoongArch boot.
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_BOOT_H
#define HW_LOONGARCH_BOOT_H

/* UEFI 2.10 */
#define EFI_SYSTEM_TABLE_SIGNATURE       0x5453595320494249
#define EFI_2_100_SYSTEM_TABLE_REVISION  ((2<<16) | (100))
#define EFI_SPECIFICATION_VERSION        EFI_SYSTEM_TABLE_REVISION
#define EFI_SYSTEM_TABLE_REVISION        EFI_2_100_SYSTEM_TABLE_REVISION

#define FW_VERSION 0x1
#define FW_PATCHLEVEL 0x0

typedef struct {
    uint8_t b[16];
} efi_guid_t QEMU_ALIGNED(8);

struct efi_config_table {
    efi_guid_t guid;
    uint64_t *ptr;
    const char name[16];
};

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t headersize;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_hdr_t;

struct efi_configuration_table {
    efi_guid_t guid;
    void *table;
};

struct efi_system_table {
    efi_table_hdr_t hdr;
    uint64_t fw_vendor;        /* physical addr of CHAR16 vendor string */
    uint32_t fw_revision;
    uint64_t con_in_handle;
    uint64_t *con_in;
    uint64_t con_out_handle;
    uint64_t *con_out;
    uint64_t stderr_handle;
    uint64_t stderr_placeholder;
    uint64_t *runtime;
    uint64_t *boottime;
    uint64_t nr_tables;
    struct efi_configuration_table *tables;
};

struct loongarch_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    uint64_t a0, a1, a2;
};

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info);

#endif /* HW_LOONGARCH_BOOT_H */
