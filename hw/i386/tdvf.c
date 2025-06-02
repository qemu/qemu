/*
 * Copyright (c) 2025 Intel Corporation
 * Author: Isaku Yamahata <isaku.yamahata at gmail.com>
 *                        <isaku.yamahata at intel.com>
 *         Xiaoyao Li <xiaoyao.li@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/i386/pc.h"
#include "hw/i386/tdvf.h"
#include "system/kvm.h"

#define TDX_METADATA_OFFSET_GUID    "e47a6535-984a-4798-865e-4685a7bf8ec2"
#define TDX_METADATA_VERSION        1
#define TDVF_SIGNATURE              0x46564454 /* TDVF as little endian */
#define TDVF_ALIGNMENT              4096

/*
 * the raw structs read from TDVF keeps the name convention in
 * TDVF Design Guide spec.
 */
typedef struct {
    uint32_t DataOffset;
    uint32_t RawDataSize;
    uint64_t MemoryAddress;
    uint64_t MemoryDataSize;
    uint32_t Type;
    uint32_t Attributes;
} TdvfSectionEntry;

typedef struct {
    uint32_t Signature;
    uint32_t Length;
    uint32_t Version;
    uint32_t NumberOfSectionEntries;
    TdvfSectionEntry SectionEntries[];
} TdvfMetadata;

struct tdx_metadata_offset {
    uint32_t offset;
};

static TdvfMetadata *tdvf_get_metadata(void *flash_ptr, int size)
{
    TdvfMetadata *metadata;
    uint32_t offset = 0;
    uint8_t *data;

    if ((uint32_t) size != size) {
        return NULL;
    }

    if (pc_system_ovmf_table_find(TDX_METADATA_OFFSET_GUID, &data, NULL)) {
        offset = size - le32_to_cpu(((struct tdx_metadata_offset *)data)->offset);

        if (offset + sizeof(*metadata) > size) {
            return NULL;
        }
    } else {
        error_report("Cannot find TDX_METADATA_OFFSET_GUID");
        return NULL;
    }

    metadata = flash_ptr + offset;

    /* Finally, verify the signature to determine if this is a TDVF image. */
    metadata->Signature = le32_to_cpu(metadata->Signature);
    if (metadata->Signature != TDVF_SIGNATURE) {
        error_report("Invalid TDVF signature in metadata!");
        return NULL;
    }

    /* Sanity check that the TDVF doesn't overlap its own metadata. */
    metadata->Length = le32_to_cpu(metadata->Length);
    if (offset + metadata->Length > size) {
        return NULL;
    }

    /* Only version 1 is supported/defined. */
    metadata->Version = le32_to_cpu(metadata->Version);
    if (metadata->Version != TDX_METADATA_VERSION) {
        return NULL;
    }

    return metadata;
}

static int tdvf_parse_and_check_section_entry(const TdvfSectionEntry *src,
                                              TdxFirmwareEntry *entry)
{
    entry->data_offset = le32_to_cpu(src->DataOffset);
    entry->data_len = le32_to_cpu(src->RawDataSize);
    entry->address = le64_to_cpu(src->MemoryAddress);
    entry->size = le64_to_cpu(src->MemoryDataSize);
    entry->type = le32_to_cpu(src->Type);
    entry->attributes = le32_to_cpu(src->Attributes);

    /* sanity check */
    if (entry->size < entry->data_len) {
        error_report("Broken metadata RawDataSize 0x%x MemoryDataSize 0x%"PRIx64,
                     entry->data_len, entry->size);
        return -1;
    }
    if (!QEMU_IS_ALIGNED(entry->address, TDVF_ALIGNMENT)) {
        error_report("MemoryAddress 0x%"PRIx64" not page aligned", entry->address);
        return -1;
    }
    if (!QEMU_IS_ALIGNED(entry->size, TDVF_ALIGNMENT)) {
        error_report("MemoryDataSize 0x%"PRIx64" not page aligned", entry->size);
        return -1;
    }

    switch (entry->type) {
    case TDVF_SECTION_TYPE_BFV:
    case TDVF_SECTION_TYPE_CFV:
        /* The sections that must be copied from firmware image to TD memory */
        if (entry->data_len == 0) {
            error_report("%d section with RawDataSize == 0", entry->type);
            return -1;
        }
        break;
    case TDVF_SECTION_TYPE_TD_HOB:
    case TDVF_SECTION_TYPE_TEMP_MEM:
        /* The sections that no need to be copied from firmware image */
        if (entry->data_len != 0) {
            error_report("%d section with RawDataSize 0x%x != 0",
                         entry->type, entry->data_len);
            return -1;
        }
        break;
    default:
        error_report("TDVF contains unsupported section type %d", entry->type);
        return -1;
    }

    return 0;
}

int tdvf_parse_metadata(TdxFirmware *fw, void *flash_ptr, int size)
{
    g_autofree TdvfSectionEntry *sections = NULL;
    TdvfMetadata *metadata;
    ssize_t entries_size;
    int i;

    metadata = tdvf_get_metadata(flash_ptr, size);
    if (!metadata) {
        return -EINVAL;
    }

    /* load and parse metadata entries */
    fw->nr_entries = le32_to_cpu(metadata->NumberOfSectionEntries);
    if (fw->nr_entries < 2) {
        error_report("Invalid number of fw entries (%u) in TDVF Metadata",
                     fw->nr_entries);
        return -EINVAL;
    }

    entries_size = fw->nr_entries * sizeof(TdvfSectionEntry);
    if (metadata->Length != sizeof(*metadata) + entries_size) {
        error_report("TDVF metadata len (0x%x) mismatch, expected (0x%x)",
                     metadata->Length,
                     (uint32_t)(sizeof(*metadata) + entries_size));
        return -EINVAL;
    }

    fw->entries = g_new(TdxFirmwareEntry, fw->nr_entries);
    sections = g_new(TdvfSectionEntry, fw->nr_entries);

    memcpy(sections, (void *)metadata + sizeof(*metadata), entries_size);

    for (i = 0; i < fw->nr_entries; i++) {
        if (tdvf_parse_and_check_section_entry(&sections[i], &fw->entries[i])) {
            goto err;
        }
    }

    fw->mem_ptr = flash_ptr;
    return 0;

err:
    fw->entries = 0;
    g_free(fw->entries);
    return -EINVAL;
}
