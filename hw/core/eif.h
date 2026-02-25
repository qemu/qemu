/*
 * EIF (Enclave Image Format) related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef HW_CORE_EIF_H
#define HW_CORE_EIF_H

#define MAX_SECTIONS 32
#define EIF_HDR_ARCH_ARM64 0x1

/* members are ordered according to field order in .eif file */
typedef struct EifHeader {
    uint8_t  magic[4]; /* must be .eif in ascii i.e., [46, 101, 105, 102] */
    uint16_t version;
    uint16_t flags;
    uint64_t default_memory;
    uint64_t default_cpus;
    uint16_t reserved;
    uint16_t section_cnt;
    uint64_t section_offsets[MAX_SECTIONS];
    uint64_t section_sizes[MAX_SECTIONS];
    uint32_t unused;
    uint32_t eif_crc32;
} QEMU_PACKED EifHeader;

/* members are ordered according to field order in .eif file */
typedef struct EifSectionHeader {
    /*
     * 0 = invalid, 1 = kernel, 2 = cmdline, 3 = ramdisk, 4 = signature,
     * 5 = metadata
     */
    uint16_t section_type;
    uint16_t flags;
    uint64_t section_size;
} QEMU_PACKED EifSectionHeader;

enum EifSectionTypes {
    EIF_SECTION_INVALID = 0,
    EIF_SECTION_KERNEL = 1,
    EIF_SECTION_CMDLINE = 2,
    EIF_SECTION_RAMDISK = 3,
    EIF_SECTION_SIGNATURE = 4,
    EIF_SECTION_METADATA = 5,
    EIF_SECTION_MAX = 6,
};

#define EIF_MAGIC { '.', 'e', 'i', 'f' }

bool read_eif_file(const char *eif_path, const char *machine_initrd,
                   char **kernel_path, char **initrd_path,
                   char **kernel_cmdline, uint8_t *image_sha384,
                   uint8_t *bootstrap_sha384, uint8_t *app_sha384,
                   uint8_t *fingerprint_sha384, bool *signature_found,
                   Error **errp);

#endif

