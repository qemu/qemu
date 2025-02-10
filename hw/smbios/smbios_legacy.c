/*
 * SMBIOS legacy support
 *
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Authors:
 *  Alex Williamson <alex.williamson@hp.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/firmware/smbios.h"
#include "system/system.h"
#include "qapi/error.h"

struct smbios_header {
    uint16_t length;
    uint8_t type;
} QEMU_PACKED;

struct smbios_field {
    struct smbios_header header;
    uint8_t type;
    uint16_t offset;
    uint8_t data[];
} QEMU_PACKED;

struct smbios_table {
    struct smbios_header header;
    uint8_t data[];
} QEMU_PACKED;

#define SMBIOS_FIELD_ENTRY 0
#define SMBIOS_TABLE_ENTRY 1

static uint8_t *smbios_entries;
static size_t smbios_entries_len;
GArray *usr_blobs_sizes;

void smbios_add_usr_blob_size(size_t size)
{
    if (!usr_blobs_sizes) {
        usr_blobs_sizes = g_array_new(false, false, sizeof(size_t));
    }
    g_array_append_val(usr_blobs_sizes, size);
}

static void smbios_add_field(int type, int offset, const void *data, size_t len)
{
    struct smbios_field *field;

    if (!smbios_entries) {
        smbios_entries_len = sizeof(uint16_t);
        smbios_entries = g_malloc0(smbios_entries_len);
    }
    smbios_entries = g_realloc(smbios_entries, smbios_entries_len +
                                                  sizeof(*field) + len);
    field = (struct smbios_field *)(smbios_entries + smbios_entries_len);
    field->header.type = SMBIOS_FIELD_ENTRY;
    field->header.length = cpu_to_le16(sizeof(*field) + len);

    field->type = type;
    field->offset = cpu_to_le16(offset);
    memcpy(field->data, data, len);

    smbios_entries_len += sizeof(*field) + len;
    (*(uint16_t *)smbios_entries) =
            cpu_to_le16(le16_to_cpu(*(uint16_t *)smbios_entries) + 1);
}

static void smbios_maybe_add_str(int type, int offset, const char *data)
{
    if (data) {
        smbios_add_field(type, offset, data, strlen(data) + 1);
    }
}

static void smbios_build_type_0_fields(void)
{
    smbios_maybe_add_str(0, offsetof(struct smbios_type_0, vendor_str),
                         smbios_type0.vendor);
    smbios_maybe_add_str(0, offsetof(struct smbios_type_0, bios_version_str),
                         smbios_type0.version);
    smbios_maybe_add_str(0, offsetof(struct smbios_type_0,
                                     bios_release_date_str),
                         smbios_type0.date);
    if (smbios_type0.have_major_minor) {
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_major_release),
                         &smbios_type0.major, 1);
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_minor_release),
                         &smbios_type0.minor, 1);
    }
}

static void smbios_build_type_1_fields(void)
{
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, manufacturer_str),
                         smbios_type1.manufacturer);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, product_name_str),
                         smbios_type1.product);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, version_str),
                         smbios_type1.version);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, serial_number_str),
                         smbios_type1.serial);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, sku_number_str),
                         smbios_type1.sku);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, family_str),
                         smbios_type1.family);
    if (qemu_uuid_set) {
        /*
         * We don't encode the UUID in the "wire format" here because this
         * function is for legacy mode and needs to keep the guest ABI, and
         * because we don't know what's the SMBIOS version advertised by the
         * BIOS.
         */
        smbios_add_field(1, offsetof(struct smbios_type_1, uuid),
                         &qemu_uuid, 16);
    }
}

uint8_t *smbios_get_table_legacy(size_t *length, Error **errp)
{
    int i;
    size_t usr_offset;

    /* complain if fields were given for types > 1 */
    if (find_next_bit(smbios_have_fields_bitmap,
                      SMBIOS_MAX_TYPE + 1, 2) < SMBIOS_MAX_TYPE + 1) {
        error_setg(errp, "can't process fields for smbios "
                     "types > 1 on machine versions < 2.1!");
        goto err_exit;
    }

    if (test_bit(4, smbios_have_binfile_bitmap)) {
        error_setg(errp, "can't process table for smbios "
                   "type 4 on machine versions < 2.1!");
        goto err_exit;
    }

    g_free(smbios_entries);
    smbios_entries_len = sizeof(uint16_t);
    smbios_entries = g_malloc0(smbios_entries_len);

    /*
     * build a set of legacy smbios_table entries using user provided blobs
     */
    for (i = 0, usr_offset = 0; usr_blobs_sizes && i < usr_blobs_sizes->len;
         i++)
    {
        struct smbios_table *table;
        struct smbios_structure_header *header;
        size_t size = g_array_index(usr_blobs_sizes, size_t, i);

        header = (struct smbios_structure_header *)(usr_blobs + usr_offset);
        smbios_entries = g_realloc(smbios_entries, smbios_entries_len +
                                                   size + sizeof(*table));
        table = (struct smbios_table *)(smbios_entries + smbios_entries_len);
        table->header.type = SMBIOS_TABLE_ENTRY;
        table->header.length = cpu_to_le16(sizeof(*table) + size);
        memcpy(table->data, header, size);
        smbios_entries_len += sizeof(*table) + size;
        /*
         * update number of entries in the blob,
         * see SeaBIOS: qemu_cfg_legacy():QEMU_CFG_SMBIOS_ENTRIES
         */
        (*(uint16_t *)smbios_entries) =
            cpu_to_le16(le16_to_cpu(*(uint16_t *)smbios_entries) + 1);
        usr_offset += size;
    }

    smbios_build_type_0_fields();
    smbios_build_type_1_fields();
    if (!smbios_validate_table(SMBIOS_ENTRY_POINT_TYPE_32, errp)) {
        goto err_exit;
    }

    *length = smbios_entries_len;
    return smbios_entries;
err_exit:
    g_free(smbios_entries);
    return NULL;
}
