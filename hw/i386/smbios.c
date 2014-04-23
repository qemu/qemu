/*
 * SMBIOS Support
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

#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "hw/i386/smbios.h"
#include "hw/loader.h"


/* legacy structures and constants for <= 2.0 machines */
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
/* end: legacy structures & constants for <= 2.0 machines */


static int smbios_type4_count = 0;
static bool smbios_immutable;

static struct {
    bool seen;
    int headertype;
    Location loc;
} first_opt[2];

static struct {
    const char *vendor, *version, *date;
    bool have_major_minor;
    uint8_t major, minor;
} type0;

static struct {
    const char *manufacturer, *product, *version, *serial, *sku, *family;
    /* uuid is in qemu_uuid[] */
} type1;

static QemuOptsList qemu_smbios_opts = {
    .name = "smbios",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_smbios_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    }
};

static const QemuOptDesc qemu_smbios_file_opts[] = {
    {
        .name = "file",
        .type = QEMU_OPT_STRING,
        .help = "binary file containing an SMBIOS element",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type0_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "vendor",
        .type = QEMU_OPT_STRING,
        .help = "vendor name",
    },{
        .name = "version",
        .type = QEMU_OPT_STRING,
        .help = "version number",
    },{
        .name = "date",
        .type = QEMU_OPT_STRING,
        .help = "release date",
    },{
        .name = "release",
        .type = QEMU_OPT_STRING,
        .help = "revision number",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type1_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "manufacturer",
        .type = QEMU_OPT_STRING,
        .help = "manufacturer name",
    },{
        .name = "product",
        .type = QEMU_OPT_STRING,
        .help = "product name",
    },{
        .name = "version",
        .type = QEMU_OPT_STRING,
        .help = "version number",
    },{
        .name = "serial",
        .type = QEMU_OPT_STRING,
        .help = "serial number",
    },{
        .name = "uuid",
        .type = QEMU_OPT_STRING,
        .help = "UUID",
    },{
        .name = "sku",
        .type = QEMU_OPT_STRING,
        .help = "SKU number",
    },{
        .name = "family",
        .type = QEMU_OPT_STRING,
        .help = "family name",
    },
    { /* end of list */ }
};

static void smbios_register_config(void)
{
    qemu_add_opts(&qemu_smbios_opts);
}

machine_init(smbios_register_config);

static void smbios_validate_table(void)
{
    if (smbios_type4_count && smbios_type4_count != smp_cpus) {
        error_report("Number of SMBIOS Type 4 tables must match cpu count");
        exit(1);
    }
}

/*
 * To avoid unresolvable overlaps in data, don't allow both
 * tables and fields for the same smbios type.
 */
static void smbios_check_collision(int type, int entry)
{
    if (type < ARRAY_SIZE(first_opt)) {
        if (first_opt[type].seen) {
            if (first_opt[type].headertype != entry) {
                error_report("Can't mix file= and type= for same type");
                loc_push_restore(&first_opt[type].loc);
                error_report("This is the conflicting setting");
                loc_pop(&first_opt[type].loc);
                exit(1);
            }
        } else {
            first_opt[type].seen = true;
            first_opt[type].headertype = entry;
            loc_save(&first_opt[type].loc);
        }
    }
}


/* legacy setup functions for <= 2.0 machines */
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
                         type0.vendor);
    smbios_maybe_add_str(0, offsetof(struct smbios_type_0, bios_version_str),
                         type0.version);
    smbios_maybe_add_str(0, offsetof(struct smbios_type_0,
                                     bios_release_date_str),
                         type0.date);
    if (type0.have_major_minor) {
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_major_release),
                         &type0.major, 1);
        smbios_add_field(0, offsetof(struct smbios_type_0,
                                     system_bios_minor_release),
                         &type0.minor, 1);
    }
}

static void smbios_build_type_1_fields(void)
{
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, manufacturer_str),
                         type1.manufacturer);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, product_name_str),
                         type1.product);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, version_str),
                         type1.version);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, serial_number_str),
                         type1.serial);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, sku_number_str),
                         type1.sku);
    smbios_maybe_add_str(1, offsetof(struct smbios_type_1, family_str),
                         type1.family);
    if (qemu_uuid_set) {
        smbios_add_field(1, offsetof(struct smbios_type_1, uuid),
                         qemu_uuid, 16);
    }
}

void smbios_set_defaults(const char *manufacturer, const char *product,
                         const char *version)
{
    if (!type1.manufacturer) {
        type1.manufacturer = manufacturer;
    }
    if (!type1.product) {
        type1.product = product;
    }
    if (!type1.version) {
        type1.version = version;
    }
}

uint8_t *smbios_get_table_legacy(size_t *length)
{
    if (!smbios_immutable) {
        smbios_build_type_0_fields();
        smbios_build_type_1_fields();
        smbios_validate_table();
        smbios_immutable = true;
    }
    *length = smbios_entries_len;
    return smbios_entries;
}
/* end: legacy setup functions for <= 2.0 machines */


static void save_opt(const char **dest, QemuOpts *opts, const char *name)
{
    const char *val = qemu_opt_get(opts, name);

    if (val) {
        *dest = val;
    }
}

void smbios_entry_add(QemuOpts *opts)
{
    Error *local_err = NULL;
    const char *val;

    assert(!smbios_immutable);
    val = qemu_opt_get(opts, "file");
    if (val) {
        struct smbios_structure_header *header;
        struct smbios_table *table;
        int size;

        qemu_opts_validate(opts, qemu_smbios_file_opts, &local_err);
        if (local_err) {
            error_report("%s", error_get_pretty(local_err));
            exit(1);
        }

        size = get_image_size(val);
        if (size == -1 || size < sizeof(struct smbios_structure_header)) {
            error_report("Cannot read SMBIOS file %s", val);
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

        if (load_image(val, table->data) != size) {
            error_report("Failed to load SMBIOS file %s", val);
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
        return;
    }

    val = qemu_opt_get(opts, "type");
    if (val) {
        unsigned long type = strtoul(val, NULL, 0);

        smbios_check_collision(type, SMBIOS_FIELD_ENTRY);

        switch (type) {
        case 0:
            qemu_opts_validate(opts, qemu_smbios_type0_opts, &local_err);
            if (local_err) {
                error_report("%s", error_get_pretty(local_err));
                exit(1);
            }
            save_opt(&type0.vendor, opts, "vendor");
            save_opt(&type0.version, opts, "version");
            save_opt(&type0.date, opts, "date");

            val = qemu_opt_get(opts, "release");
            if (val) {
                if (sscanf(val, "%hhu.%hhu", &type0.major, &type0.minor) != 2) {
                    error_report("Invalid release");
                    exit(1);
                }
                type0.have_major_minor = true;
            }
            return;
        case 1:
            qemu_opts_validate(opts, qemu_smbios_type1_opts, &local_err);
            if (local_err) {
                error_report("%s", error_get_pretty(local_err));
                exit(1);
            }
            save_opt(&type1.manufacturer, opts, "manufacturer");
            save_opt(&type1.product, opts, "product");
            save_opt(&type1.version, opts, "version");
            save_opt(&type1.serial, opts, "serial");
            save_opt(&type1.sku, opts, "sku");
            save_opt(&type1.family, opts, "family");

            val = qemu_opt_get(opts, "uuid");
            if (val) {
                if (qemu_uuid_parse(val, qemu_uuid) != 0) {
                    error_report("Invalid UUID");
                    exit(1);
                }
                qemu_uuid_set = true;
            }
            return;
        default:
            error_report("Don't know how to build fields for SMBIOS type %ld",
                         type);
            exit(1);
        }
    }

    error_report("Must specify type= or file=");
    exit(1);
}
