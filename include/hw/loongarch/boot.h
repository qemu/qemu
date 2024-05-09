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

#define EFI_GUID(a, b, c, d...) (efi_guid_t){ {                                \
        (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
        (b) & 0xff, ((b) >> 8) & 0xff,                                         \
        (c) & 0xff, ((c) >> 8) & 0xff, d } }

#define LINUX_EFI_BOOT_MEMMAP_GUID \
        EFI_GUID(0x800f683f, 0xd08b, 0x423a,  0xa2, 0x93, \
                 0x96, 0x5c, 0x3c, 0x6f, 0xe2, 0xb4)

#define LINUX_EFI_INITRD_MEDIA_GUID \
        EFI_GUID(0x5568e427, 0x68fc, 0x4f3d,  0xac, 0x74, \
                 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68)

#define DEVICE_TREE_GUID \
        EFI_GUID(0xb1b621d5, 0xf19c, 0x41a5,  0x83, 0x0b, \
                 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0)

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

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint64_t num_pages;
    uint64_t attribute;
} efi_memory_desc_t;

struct efi_boot_memmap {
    uint64_t map_size;
    uint64_t desc_size;
    uint32_t desc_ver;
    uint64_t map_key;
    uint64_t buff_size;
    efi_memory_desc_t map[32];
};

struct efi_initrd {
    uint64_t base;
    uint64_t size;
};

struct loongarch_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    uint64_t a0, a1, a2;
};

extern struct memmap_entry *memmap_table;
extern unsigned memmap_entries;

struct memmap_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info);

#endif /* HW_LOONGARCH_BOOT_H */
