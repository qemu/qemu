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
 */

#include "sysemu.h"
#include "smbios.h"

/*
 * Structures shared with the BIOS
 */
struct smbios_header {
    uint16_t length;
    uint8_t type;
} __attribute__((__packed__));

struct smbios_field {
    struct smbios_header header;
    uint8_t type;
    uint16_t offset;
    uint8_t data[];
} __attribute__((__packed__));

struct smbios_table {
    struct smbios_header header;
    uint8_t data[];
} __attribute__((__packed__));

#define SMBIOS_FIELD_ENTRY 0
#define SMBIOS_TABLE_ENTRY 1


static uint8_t *smbios_entries;
static size_t smbios_entries_len;

uint8_t *smbios_get_table(size_t *length)
{
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
                fprintf(stderr, "SMBIOS type %d field already defined, "
                                "cannot add table\n", type);
                exit(1);
            }
        } else if (entry == SMBIOS_FIELD_ENTRY &&
                   header->type == SMBIOS_TABLE_ENTRY) {
            struct smbios_structure_header *table = (void *)(header + 1);
            if (type == table->type) {
                fprintf(stderr, "SMBIOS type %d table already defined, "
                                "cannot add field\n", type);
                exit(1);
            }
        }
        p += le16_to_cpu(header->length);
    }
}

void smbios_add_field(int type, int offset, int len, void *data)
{
    struct smbios_field *field;

    smbios_check_collision(type, SMBIOS_FIELD_ENTRY);

    if (!smbios_entries) {
        smbios_entries_len = sizeof(uint16_t);
        smbios_entries = qemu_mallocz(smbios_entries_len);
    }
    smbios_entries = qemu_realloc(smbios_entries, smbios_entries_len +
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

    if (get_param_value(buf, sizeof(buf), "vendor", t))
        smbios_add_field(0, offsetof(struct smbios_type_0, vendor_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "version", t))
        smbios_add_field(0, offsetof(struct smbios_type_0, bios_version_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "date", t))
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     bios_release_date_str),
                                     strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "release", t)) {
        int major, minor;
        sscanf(buf, "%d.%d", &major, &minor);
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_major_release), 1, &major);
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_minor_release), 1, &minor);
    }
}

static void smbios_build_type_1_fields(const char *t)
{
    char buf[1024];

    if (get_param_value(buf, sizeof(buf), "manufacturer", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, manufacturer_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "product", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, product_name_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "version", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, version_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "serial", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, serial_number_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "uuid", t)) {
        if (qemu_uuid_parse(buf, qemu_uuid) != 0) {
            fprintf(stderr, "Invalid SMBIOS UUID string\n");
            exit(1);
        }
    }
    if (get_param_value(buf, sizeof(buf), "sku", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, sku_number_str),
                         strlen(buf) + 1, buf);
    if (get_param_value(buf, sizeof(buf), "family", t))
        smbios_add_field(1, offsetof(struct smbios_type_1, family_str),
                         strlen(buf) + 1, buf);
}

int smbios_entry_add(const char *t)
{
    char buf[1024];

    if (get_param_value(buf, sizeof(buf), "file", t)) {
        struct smbios_structure_header *header;
        struct smbios_table *table;
        int size = get_image_size(buf);

        if (size < sizeof(struct smbios_structure_header)) {
            fprintf(stderr, "Cannot read smbios file %s", buf);
            exit(1);
        }

        if (!smbios_entries) {
            smbios_entries_len = sizeof(uint16_t);
            smbios_entries = qemu_mallocz(smbios_entries_len);
        }

        smbios_entries = qemu_realloc(smbios_entries, smbios_entries_len +
                                                      sizeof(*table) + size);
        table = (struct smbios_table *)(smbios_entries + smbios_entries_len);
        table->header.type = SMBIOS_TABLE_ENTRY;
        table->header.length = cpu_to_le16(sizeof(*table) + size);

        if (load_image(buf, table->data) != size) {
            fprintf(stderr, "Failed to load smbios file %s", buf);
            exit(1);
        }

        header = (struct smbios_structure_header *)(table->data);
        smbios_check_collision(header->type, SMBIOS_TABLE_ENTRY);

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
            fprintf(stderr, "Don't know how to build fields for SMBIOS type "
                    "%ld\n", type);
            exit(1);
        }
    }

    fprintf(stderr, "smbios: must specify type= or file=\n");
    return -1;
}
