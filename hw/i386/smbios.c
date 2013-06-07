/*
 * SMBIOS Support
 *
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *
 * Authors:
 *  Alex Williamson <alex.williamson@hp.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "hw/i386/smbios.h"
#include "hw/loader.h"

/*
 * Structures shared with the BIOS
 */
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
static int smbios_type4_count = 0;

static void smbios_validate_table(void)
{
    if (smbios_type4_count && smbios_type4_count != smp_cpus) {
        error_report("Number of SMBIOS Type 4 tables must match cpu count");
        exit(1);
    }
}

uint8_t *smbios_get_table(size_t *length)
{
    smbios_validate_table();
    *length = smbios_entries_len;
    return smbios_entries;
}

/*
 * To avoid unresolvable overlaps in data, don't allow both
 * tables and fields for the same smbios type.
 */
static void smbios_check_collision(int type, int entry)
{
    uint16_t *num_entries = (uint16_t *)smbios_entries;
    struct smbios_header *header;
    char *p;
    int i;

    if (!num_entries)
        return;

    p = (char *)(num_entries + 1);

    for (i = 0; i < *num_entries; i++) {
        header = (struct smbios_header *)p;
        if (entry == SMBIOS_TABLE_ENTRY && header->type == SMBIOS_FIELD_ENTRY) {
            struct smbios_field *field = (void *)header;
            if (type == field->type) {
                error_report("SMBIOS type %d field already defined, "
                             "cannot add table", type);
                exit(1);
            }
        } else if (entry == SMBIOS_FIELD_ENTRY &&
                   header->type == SMBIOS_TABLE_ENTRY) {
            struct smbios_structure_header *table = (void *)(header + 1);
            if (type == table->type) {
                error_report("SMBIOS type %d table already defined, "
                             "cannot add field", type);
                exit(1);
            }
        }
        p += le16_to_cpu(header->length);
    }
}

void smbios_add_field(int type, int offset, const void *data, size_t len)
{
    struct smbios_field *field;

    smbios_check_collision(type, SMBIOS_FIELD_ENTRY);

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

static void smbios_build_type_0_fields(const char *t)
{
    char buf[1024];
    unsigned char major, minor;

    if (get_param_value(buf, sizeof(buf), "vendor", t))
        smbios_add_field(0, offsetof(struct smbios_type_0, vendor_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "version", t))
        smbios_add_field(0, offsetof(struct smbios_type_0, bios_version_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "date", t))
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     bios_release_date_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "release", t)) {
        if (sscanf(buf, "%hhu.%hhu", &major, &minor) != 2) {
            error_report("Invalid release");
            exit(1);
        }
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_major_release),
                         &major, 1);
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_minor_release),
                         &minor, 1);
    }
}

static void smbios_build_type_1_fields(const char *t)
{
    char buf[1024];

    if (get_param_value(buf, sizeof(buf), "manufacturer", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, manufacturer_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "product", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, product_name_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "version", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, version_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "serial", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, serial_number_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "uuid", t)) {
        if (qemu_uuid_parse(buf, qemu_uuid) != 0) {
            error_report("Invalid UUID");
            exit(1);
        }
    }
    if (get_param_value(buf, sizeof(buf), "sku", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, sku_number_str),
                         buf, strlen(buf) + 1);
    if (get_param_value(buf, sizeof(buf), "family", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, family_str),
                         buf, strlen(buf) + 1);
}

int smbios_entry_add(const char *t)
{
    char buf[1024];

    if (get_param_value(buf, sizeof(buf), "file", t)) {
        struct smbios_structure_header *header;
        struct smbios_table *table;
        int size = get_image_size(buf);

        if (size == -1 || size < sizeof(struct smbios_structure_header)) {
            error_report("Cannot read SMBIOS file %s", buf);
            exit(1);
        }

        if (!smbios_entries) {
            smbios_entries_len = sizeof(uint16_t);
            smbios_entries = g_malloc0(smbios_entries_len);
        }

        smbios_entries = g_realloc(smbios_entries, smbios_entries_len +
                                                      sizeof(*table) + size);
        table = (struct smbios_table *)(smbios_entries + smbios_entries_len);
        table->header.type = SMBIOS_TABLE_ENTRY;
        table->header.length = cpu_to_le16(sizeof(*table) + size);

        if (load_image(buf, table->data) != size) {
            error_report("Failed to load SMBIOS file %s", buf);
            exit(1);
        }

        header = (struct smbios_structure_header *)(table->data);
        smbios_check_collision(header->type, SMBIOS_TABLE_ENTRY);
        if (header->type == 4) {
            smbios_type4_count++;
        }

        smbios_entries_len += sizeof(*table) + size;
        (*(uint16_t *)smbios_entries) =
                cpu_to_le16(le16_to_cpu(*(uint16_t *)smbios_entries) + 1);
        return 0;
    }

    if (get_param_value(buf, sizeof(buf), "type", t)) {
        unsigned long type = strtoul(buf, NULL, 0);
        switch (type) {
        case 0:
            smbios_build_type_0_fields(t);
            return 0;
        case 1:
            smbios_build_type_1_fields(t);
            return 0;
        default:
            error_report("Don't know how to build fields for SMBIOS type %ld",
                         type);
            exit(1);
        }
    }

    error_report("Must specify type= or file=");
    return -1;
}
