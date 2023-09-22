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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "qemu/uuid.h"
#include "hw/firmware/smbios.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_device.h"
#include "smbios_build.h"

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
static bool smbios_legacy = true;
static bool smbios_uuid_encoded = true;
/* end: legacy structures & constants for <= 2.0 machines */


uint8_t *smbios_tables;
size_t smbios_tables_len;
unsigned smbios_table_max;
unsigned smbios_table_cnt;
static SmbiosEntryPointType smbios_ep_type = SMBIOS_ENTRY_POINT_TYPE_32;

static SmbiosEntryPoint ep;

static int smbios_type4_count = 0;
static bool smbios_immutable;
static bool smbios_have_defaults;
static uint32_t smbios_cpuid_version, smbios_cpuid_features, smbios_smp_sockets;

static DECLARE_BITMAP(have_binfile_bitmap, SMBIOS_MAX_TYPE+1);
static DECLARE_BITMAP(have_fields_bitmap, SMBIOS_MAX_TYPE+1);

static struct {
    const char *vendor, *version, *date;
    bool have_major_minor, uefi;
    uint8_t major, minor;
} type0;

static struct {
    const char *manufacturer, *product, *version, *serial, *sku, *family;
    /* uuid is in qemu_uuid */
} type1;

static struct {
    const char *manufacturer, *product, *version, *serial, *asset, *location;
} type2;

static struct {
    const char *manufacturer, *version, *serial, *asset, *sku;
} type3;

/*
 * SVVP requires max_speed and current_speed to be set and not being
 * 0 which counts as unknown (SMBIOS 3.1.0/Table 21). Set the
 * default value to 2000MHz as we did before.
 */
#define DEFAULT_CPU_SPEED 2000

static struct {
    const char *sock_pfx, *manufacturer, *version, *serial, *asset, *part;
    uint64_t max_speed;
    uint64_t current_speed;
    uint64_t processor_id;
} type4 = {
    .max_speed = DEFAULT_CPU_SPEED,
    .current_speed = DEFAULT_CPU_SPEED,
    .processor_id = 0,
};

struct type8_instance {
    const char *internal_reference, *external_reference;
    uint8_t connector_type, port_type;
    QTAILQ_ENTRY(type8_instance) next;
};
static QTAILQ_HEAD(, type8_instance) type8 = QTAILQ_HEAD_INITIALIZER(type8);

static struct {
    size_t nvalues;
    char **values;
} type11;

static struct {
    const char *loc_pfx, *bank, *manufacturer, *serial, *asset, *part;
    uint16_t speed;
} type17;

static QEnumLookup type41_kind_lookup = {
    .array = (const char *const[]) {
        "other",
        "unknown",
        "video",
        "scsi",
        "ethernet",
        "tokenring",
        "sound",
        "pata",
        "sata",
        "sas",
    },
    .size = 10
};
struct type41_instance {
    const char *designation, *pcidev;
    uint8_t instance, kind;
    QTAILQ_ENTRY(type41_instance) next;
};
static QTAILQ_HEAD(, type41_instance) type41 = QTAILQ_HEAD_INITIALIZER(type41);

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
    },{
        .name = "uefi",
        .type = QEMU_OPT_BOOL,
        .help = "uefi support",
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

static const QemuOptDesc qemu_smbios_type2_opts[] = {
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
        .name = "asset",
        .type = QEMU_OPT_STRING,
        .help = "asset tag number",
    },{
        .name = "location",
        .type = QEMU_OPT_STRING,
        .help = "location in chassis",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type3_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "manufacturer",
        .type = QEMU_OPT_STRING,
        .help = "manufacturer name",
    },{
        .name = "version",
        .type = QEMU_OPT_STRING,
        .help = "version number",
    },{
        .name = "serial",
        .type = QEMU_OPT_STRING,
        .help = "serial number",
    },{
        .name = "asset",
        .type = QEMU_OPT_STRING,
        .help = "asset tag number",
    },{
        .name = "sku",
        .type = QEMU_OPT_STRING,
        .help = "SKU number",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type4_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "sock_pfx",
        .type = QEMU_OPT_STRING,
        .help = "socket designation string prefix",
    },{
        .name = "manufacturer",
        .type = QEMU_OPT_STRING,
        .help = "manufacturer name",
    },{
        .name = "version",
        .type = QEMU_OPT_STRING,
        .help = "version number",
    },{
        .name = "max-speed",
        .type = QEMU_OPT_NUMBER,
        .help = "max speed in MHz",
    },{
        .name = "current-speed",
        .type = QEMU_OPT_NUMBER,
        .help = "speed at system boot in MHz",
    },{
        .name = "serial",
        .type = QEMU_OPT_STRING,
        .help = "serial number",
    },{
        .name = "asset",
        .type = QEMU_OPT_STRING,
        .help = "asset tag number",
    },{
        .name = "part",
        .type = QEMU_OPT_STRING,
        .help = "part number",
    }, {
        .name = "processor-id",
        .type = QEMU_OPT_NUMBER,
        .help = "processor id",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type8_opts[] = {
    {
        .name = "internal_reference",
        .type = QEMU_OPT_STRING,
        .help = "internal reference designator",
    },
    {
        .name = "external_reference",
        .type = QEMU_OPT_STRING,
        .help = "external reference designator",
    },
    {
        .name = "connector_type",
        .type = QEMU_OPT_NUMBER,
        .help = "connector type",
    },
    {
        .name = "port_type",
        .type = QEMU_OPT_NUMBER,
        .help = "port type",
    },
};

static const QemuOptDesc qemu_smbios_type11_opts[] = {
    {
        .name = "value",
        .type = QEMU_OPT_STRING,
        .help = "OEM string data",
    },
    {
        .name = "path",
        .type = QEMU_OPT_STRING,
        .help = "OEM string data from file",
    },
};

static const QemuOptDesc qemu_smbios_type17_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "loc_pfx",
        .type = QEMU_OPT_STRING,
        .help = "device locator string prefix",
    },{
        .name = "bank",
        .type = QEMU_OPT_STRING,
        .help = "bank locator string",
    },{
        .name = "manufacturer",
        .type = QEMU_OPT_STRING,
        .help = "manufacturer name",
    },{
        .name = "serial",
        .type = QEMU_OPT_STRING,
        .help = "serial number",
    },{
        .name = "asset",
        .type = QEMU_OPT_STRING,
        .help = "asset tag number",
    },{
        .name = "part",
        .type = QEMU_OPT_STRING,
        .help = "part number",
    },{
        .name = "speed",
        .type = QEMU_OPT_NUMBER,
        .help = "maximum capable speed",
    },
    { /* end of list */ }
};

static const QemuOptDesc qemu_smbios_type41_opts[] = {
    {
        .name = "type",
        .type = QEMU_OPT_NUMBER,
        .help = "SMBIOS element type",
    },{
        .name = "designation",
        .type = QEMU_OPT_STRING,
        .help = "reference designation string",
    },{
        .name = "kind",
        .type = QEMU_OPT_STRING,
        .help = "device type",
        .def_value_str = "other",
    },{
        .name = "instance",
        .type = QEMU_OPT_NUMBER,
        .help = "device type instance",
    },{
        .name = "pcidev",
        .type = QEMU_OPT_STRING,
        .help = "PCI device",
    },
    { /* end of list */ }
};

static void smbios_register_config(void)
{
    qemu_add_opts(&qemu_smbios_opts);
}

opts_init(smbios_register_config);

/*
 * The SMBIOS 2.1 "structure table length" field in the
 * entry point uses a 16-bit integer, so we're limited
 * in total table size
 */
#define SMBIOS_21_MAX_TABLES_LEN 0xffff

static void smbios_validate_table(MachineState *ms)
{
    uint32_t expect_t4_count = smbios_legacy ?
                                        ms->smp.cpus : smbios_smp_sockets;

    if (smbios_type4_count && smbios_type4_count != expect_t4_count) {
        error_report("Expected %d SMBIOS Type 4 tables, got %d instead",
                     expect_t4_count, smbios_type4_count);
        exit(1);
    }

    if (smbios_ep_type == SMBIOS_ENTRY_POINT_TYPE_32 &&
        smbios_tables_len > SMBIOS_21_MAX_TABLES_LEN) {
        error_report("SMBIOS 2.1 table length %zu exceeds %d",
                     smbios_tables_len, SMBIOS_21_MAX_TABLES_LEN);
        exit(1);
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
        /* We don't encode the UUID in the "wire format" here because this
         * function is for legacy mode and needs to keep the guest ABI, and
         * because we don't know what's the SMBIOS version advertised by the
         * BIOS.
         */
        smbios_add_field(1, offsetof(struct smbios_type_1, uuid),
                         &qemu_uuid, 16);
    }
}

uint8_t *smbios_get_table_legacy(MachineState *ms, size_t *length)
{
    if (!smbios_legacy) {
        *length = 0;
        return NULL;
    }

    if (!smbios_immutable) {
        smbios_build_type_0_fields();
        smbios_build_type_1_fields();
        smbios_validate_table(ms);
        smbios_immutable = true;
    }
    *length = smbios_entries_len;
    return smbios_entries;
}
/* end: legacy setup functions for <= 2.0 machines */


bool smbios_skip_table(uint8_t type, bool required_table)
{
    if (test_bit(type, have_binfile_bitmap)) {
        return true; /* user provided their own binary blob(s) */
    }
    if (test_bit(type, have_fields_bitmap)) {
        return false; /* user provided fields via command line */
    }
    if (smbios_have_defaults && required_table) {
        return false; /* we're building tables, and this one's required */
    }
    return true;
}

#define T0_BASE 0x000
#define T1_BASE 0x100
#define T2_BASE 0x200
#define T3_BASE 0x300
#define T4_BASE 0x400
#define T11_BASE 0xe00

#define T16_BASE 0x1000
#define T17_BASE 0x1100
#define T19_BASE 0x1300
#define T32_BASE 0x2000
#define T41_BASE 0x2900
#define T127_BASE 0x7F00

static void smbios_build_type_0_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(0, T0_BASE, false); /* optional, leave up to BIOS */

    SMBIOS_TABLE_SET_STR(0, vendor_str, type0.vendor);
    SMBIOS_TABLE_SET_STR(0, bios_version_str, type0.version);

    t->bios_starting_address_segment = cpu_to_le16(0xE800); /* from SeaBIOS */

    SMBIOS_TABLE_SET_STR(0, bios_release_date_str, type0.date);

    t->bios_rom_size = 0; /* hardcoded in SeaBIOS with FIXME comment */

    t->bios_characteristics = cpu_to_le64(0x08); /* Not supported */
    t->bios_characteristics_extension_bytes[0] = 0;
    t->bios_characteristics_extension_bytes[1] = 0x14; /* TCD/SVVP | VM */
    if (type0.uefi) {
        t->bios_characteristics_extension_bytes[1] |= 0x08; /* |= UEFI */
    }

    if (type0.have_major_minor) {
        t->system_bios_major_release = type0.major;
        t->system_bios_minor_release = type0.minor;
    } else {
        t->system_bios_major_release = 0;
        t->system_bios_minor_release = 0;
    }

    /* hardcoded in SeaBIOS */
    t->embedded_controller_major_release = 0xFF;
    t->embedded_controller_minor_release = 0xFF;

    SMBIOS_BUILD_TABLE_POST;
}

/* Encode UUID from the big endian encoding described on RFC4122 to the wire
 * format specified by SMBIOS version 2.6.
 */
static void smbios_encode_uuid(struct smbios_uuid *uuid, QemuUUID *in)
{
    memcpy(uuid, in, 16);
    if (smbios_uuid_encoded) {
        uuid->time_low = bswap32(uuid->time_low);
        uuid->time_mid = bswap16(uuid->time_mid);
        uuid->time_hi_and_version = bswap16(uuid->time_hi_and_version);
    }
}

static void smbios_build_type_1_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(1, T1_BASE, true); /* required */

    SMBIOS_TABLE_SET_STR(1, manufacturer_str, type1.manufacturer);
    SMBIOS_TABLE_SET_STR(1, product_name_str, type1.product);
    SMBIOS_TABLE_SET_STR(1, version_str, type1.version);
    SMBIOS_TABLE_SET_STR(1, serial_number_str, type1.serial);
    if (qemu_uuid_set) {
        smbios_encode_uuid(&t->uuid, &qemu_uuid);
    } else {
        memset(&t->uuid, 0, 16);
    }
    t->wake_up_type = 0x06; /* power switch */
    SMBIOS_TABLE_SET_STR(1, sku_number_str, type1.sku);
    SMBIOS_TABLE_SET_STR(1, family_str, type1.family);

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_2_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(2, T2_BASE, false); /* optional */

    SMBIOS_TABLE_SET_STR(2, manufacturer_str, type2.manufacturer);
    SMBIOS_TABLE_SET_STR(2, product_str, type2.product);
    SMBIOS_TABLE_SET_STR(2, version_str, type2.version);
    SMBIOS_TABLE_SET_STR(2, serial_number_str, type2.serial);
    SMBIOS_TABLE_SET_STR(2, asset_tag_number_str, type2.asset);
    t->feature_flags = 0x01; /* Motherboard */
    SMBIOS_TABLE_SET_STR(2, location_str, type2.location);
    t->chassis_handle = cpu_to_le16(0x300); /* Type 3 (System enclosure) */
    t->board_type = 0x0A; /* Motherboard */
    t->contained_element_count = 0;

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_3_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(3, T3_BASE, true); /* required */

    SMBIOS_TABLE_SET_STR(3, manufacturer_str, type3.manufacturer);
    t->type = 0x01; /* Other */
    SMBIOS_TABLE_SET_STR(3, version_str, type3.version);
    SMBIOS_TABLE_SET_STR(3, serial_number_str, type3.serial);
    SMBIOS_TABLE_SET_STR(3, asset_tag_number_str, type3.asset);
    t->boot_up_state = 0x03; /* Safe */
    t->power_supply_state = 0x03; /* Safe */
    t->thermal_state = 0x03; /* Safe */
    t->security_status = 0x02; /* Unknown */
    t->oem_defined = cpu_to_le32(0);
    t->height = 0;
    t->number_of_power_cords = 0;
    t->contained_element_count = 0;
    t->contained_element_record_length = 0;
    SMBIOS_TABLE_SET_STR(3, sku_number_str, type3.sku);

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_4_table(MachineState *ms, unsigned instance)
{
    char sock_str[128];
    size_t tbl_len = SMBIOS_TYPE_4_LEN_V28;
    unsigned threads_per_socket;
    unsigned cores_per_socket;

    if (smbios_ep_type == SMBIOS_ENTRY_POINT_TYPE_64) {
        tbl_len = SMBIOS_TYPE_4_LEN_V30;
    }

    SMBIOS_BUILD_TABLE_PRE_SIZE(4, T4_BASE + instance,
                                true, tbl_len); /* required */

    snprintf(sock_str, sizeof(sock_str), "%s%2x", type4.sock_pfx, instance);
    SMBIOS_TABLE_SET_STR(4, socket_designation_str, sock_str);
    t->processor_type = 0x03; /* CPU */
    t->processor_family = 0x01; /* Other */
    SMBIOS_TABLE_SET_STR(4, processor_manufacturer_str, type4.manufacturer);
    if (type4.processor_id == 0) {
        t->processor_id[0] = cpu_to_le32(smbios_cpuid_version);
        t->processor_id[1] = cpu_to_le32(smbios_cpuid_features);
    } else {
        t->processor_id[0] = cpu_to_le32((uint32_t)type4.processor_id);
        t->processor_id[1] = cpu_to_le32(type4.processor_id >> 32);
    }
    SMBIOS_TABLE_SET_STR(4, processor_version_str, type4.version);
    t->voltage = 0;
    t->external_clock = cpu_to_le16(0); /* Unknown */
    t->max_speed = cpu_to_le16(type4.max_speed);
    t->current_speed = cpu_to_le16(type4.current_speed);
    t->status = 0x41; /* Socket populated, CPU enabled */
    t->processor_upgrade = 0x01; /* Other */
    t->l1_cache_handle = cpu_to_le16(0xFFFF); /* N/A */
    t->l2_cache_handle = cpu_to_le16(0xFFFF); /* N/A */
    t->l3_cache_handle = cpu_to_le16(0xFFFF); /* N/A */
    SMBIOS_TABLE_SET_STR(4, serial_number_str, type4.serial);
    SMBIOS_TABLE_SET_STR(4, asset_tag_number_str, type4.asset);
    SMBIOS_TABLE_SET_STR(4, part_number_str, type4.part);

    threads_per_socket = machine_topo_get_threads_per_socket(ms);
    cores_per_socket = machine_topo_get_cores_per_socket(ms);

    t->core_count = (cores_per_socket > 255) ? 0xFF : cores_per_socket;
    t->core_enabled = t->core_count;

    t->thread_count = (threads_per_socket > 255) ? 0xFF : threads_per_socket;

    t->processor_characteristics = cpu_to_le16(0x02); /* Unknown */
    t->processor_family2 = cpu_to_le16(0x01); /* Other */

    if (tbl_len == SMBIOS_TYPE_4_LEN_V30) {
        t->core_count2 = t->core_enabled2 = cpu_to_le16(cores_per_socket);
        t->thread_count2 = cpu_to_le16(threads_per_socket);
    }

    SMBIOS_BUILD_TABLE_POST;
    smbios_type4_count++;
}

static void smbios_build_type_8_table(void)
{
    unsigned instance = 0;
    struct type8_instance *t8;

    QTAILQ_FOREACH(t8, &type8, next) {
        SMBIOS_BUILD_TABLE_PRE(8, T0_BASE + instance, true);

        SMBIOS_TABLE_SET_STR(8, internal_reference_str, t8->internal_reference);
        SMBIOS_TABLE_SET_STR(8, external_reference_str, t8->external_reference);
        /* most vendors seem to set this to None */
        t->internal_connector_type = 0x0;
        t->external_connector_type = t8->connector_type;
        t->port_type = t8->port_type;

        SMBIOS_BUILD_TABLE_POST;
        instance++;
    }
}

static void smbios_build_type_11_table(void)
{
    char count_str[128];
    size_t i;

    if (type11.nvalues == 0) {
        return;
    }

    SMBIOS_BUILD_TABLE_PRE(11, T11_BASE, true); /* required */

    snprintf(count_str, sizeof(count_str), "%zu", type11.nvalues);
    t->count = type11.nvalues;

    for (i = 0; i < type11.nvalues; i++) {
        SMBIOS_TABLE_SET_STR_LIST(11, type11.values[i]);
        g_free(type11.values[i]);
        type11.values[i] = NULL;
    }

    SMBIOS_BUILD_TABLE_POST;
}

#define MAX_T16_STD_SZ 0x80000000 /* 2T in Kilobytes */

static void smbios_build_type_16_table(unsigned dimm_cnt)
{
    uint64_t size_kb;

    SMBIOS_BUILD_TABLE_PRE(16, T16_BASE, true); /* required */

    t->location = 0x01; /* Other */
    t->use = 0x03; /* System memory */
    t->error_correction = 0x06; /* Multi-bit ECC (for Microsoft, per SeaBIOS) */
    size_kb = QEMU_ALIGN_UP(current_machine->ram_size, KiB) / KiB;
    if (size_kb < MAX_T16_STD_SZ) {
        t->maximum_capacity = cpu_to_le32(size_kb);
        t->extended_maximum_capacity = cpu_to_le64(0);
    } else {
        t->maximum_capacity = cpu_to_le32(MAX_T16_STD_SZ);
        t->extended_maximum_capacity = cpu_to_le64(current_machine->ram_size);
    }
    t->memory_error_information_handle = cpu_to_le16(0xFFFE); /* Not provided */
    t->number_of_memory_devices = cpu_to_le16(dimm_cnt);

    SMBIOS_BUILD_TABLE_POST;
}

#define MAX_T17_STD_SZ 0x7FFF /* (32G - 1M), in Megabytes */
#define MAX_T17_EXT_SZ 0x80000000 /* 2P, in Megabytes */

static void smbios_build_type_17_table(unsigned instance, uint64_t size)
{
    char loc_str[128];
    uint64_t size_mb;

    SMBIOS_BUILD_TABLE_PRE(17, T17_BASE + instance, true); /* required */

    t->physical_memory_array_handle = cpu_to_le16(0x1000); /* Type 16 above */
    t->memory_error_information_handle = cpu_to_le16(0xFFFE); /* Not provided */
    t->total_width = cpu_to_le16(0xFFFF); /* Unknown */
    t->data_width = cpu_to_le16(0xFFFF); /* Unknown */
    size_mb = QEMU_ALIGN_UP(size, MiB) / MiB;
    if (size_mb < MAX_T17_STD_SZ) {
        t->size = cpu_to_le16(size_mb);
        t->extended_size = cpu_to_le32(0);
    } else {
        assert(size_mb < MAX_T17_EXT_SZ);
        t->size = cpu_to_le16(MAX_T17_STD_SZ);
        t->extended_size = cpu_to_le32(size_mb);
    }
    t->form_factor = 0x09; /* DIMM */
    t->device_set = 0; /* Not in a set */
    snprintf(loc_str, sizeof(loc_str), "%s %d", type17.loc_pfx, instance);
    SMBIOS_TABLE_SET_STR(17, device_locator_str, loc_str);
    SMBIOS_TABLE_SET_STR(17, bank_locator_str, type17.bank);
    t->memory_type = 0x07; /* RAM */
    t->type_detail = cpu_to_le16(0x02); /* Other */
    t->speed = cpu_to_le16(type17.speed);
    SMBIOS_TABLE_SET_STR(17, manufacturer_str, type17.manufacturer);
    SMBIOS_TABLE_SET_STR(17, serial_number_str, type17.serial);
    SMBIOS_TABLE_SET_STR(17, asset_tag_number_str, type17.asset);
    SMBIOS_TABLE_SET_STR(17, part_number_str, type17.part);
    t->attributes = 0; /* Unknown */
    t->configured_clock_speed = t->speed; /* reuse value for max speed */
    t->minimum_voltage = cpu_to_le16(0); /* Unknown */
    t->maximum_voltage = cpu_to_le16(0); /* Unknown */
    t->configured_voltage = cpu_to_le16(0); /* Unknown */

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_19_table(unsigned instance, unsigned offset,
                                       uint64_t start, uint64_t size)
{
    uint64_t end, start_kb, end_kb;

    SMBIOS_BUILD_TABLE_PRE(19, T19_BASE + offset + instance,
                           true); /* required */

    end = start + size - 1;
    assert(end > start);
    start_kb = start / KiB;
    end_kb = end / KiB;
    if (start_kb < UINT32_MAX && end_kb < UINT32_MAX) {
        t->starting_address = cpu_to_le32(start_kb);
        t->ending_address = cpu_to_le32(end_kb);
        t->extended_starting_address =
            t->extended_ending_address = cpu_to_le64(0);
    } else {
        t->starting_address = t->ending_address = cpu_to_le32(UINT32_MAX);
        t->extended_starting_address = cpu_to_le64(start);
        t->extended_ending_address = cpu_to_le64(end);
    }
    t->memory_array_handle = cpu_to_le16(0x1000); /* Type 16 above */
    t->partition_width = 1; /* One device per row */

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_32_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(32, T32_BASE, true); /* required */

    memset(t->reserved, 0, 6);
    t->boot_status = 0; /* No errors detected */

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_build_type_41_table(Error **errp)
{
    unsigned instance = 0;
    struct type41_instance *t41;

    QTAILQ_FOREACH(t41, &type41, next) {
        SMBIOS_BUILD_TABLE_PRE(41, T41_BASE + instance, true);

        SMBIOS_TABLE_SET_STR(41, reference_designation_str, t41->designation);
        t->device_type = t41->kind;
        t->device_type_instance = t41->instance;
        t->segment_group_number = cpu_to_le16(0);
        t->bus_number = 0;
        t->device_number = 0;

        if (t41->pcidev) {
            PCIDevice *pdev = NULL;
            int rc = pci_qdev_find_device(t41->pcidev, &pdev);
            if (rc != 0) {
                error_setg(errp,
                           "No PCI device %s for SMBIOS type 41 entry %s",
                           t41->pcidev, t41->designation);
                return;
            }
            /*
             * We only handle the case were the device is attached to
             * the PCI root bus. The general case is more complex as
             * bridges are enumerated later and the table would need
             * to be updated at this moment.
             */
            if (!pci_bus_is_root(pci_get_bus(pdev))) {
                error_setg(errp,
                           "Cannot create type 41 entry for PCI device %s: "
                           "not attached to the root bus",
                           t41->pcidev);
                return;
            }
            t->segment_group_number = cpu_to_le16(0);
            t->bus_number = pci_dev_bus_num(pdev);
            t->device_number = pdev->devfn;
        }

        SMBIOS_BUILD_TABLE_POST;
        instance++;
    }
}

static void smbios_build_type_127_table(void)
{
    SMBIOS_BUILD_TABLE_PRE(127, T127_BASE, true); /* required */
    SMBIOS_BUILD_TABLE_POST;
}

void smbios_set_cpuid(uint32_t version, uint32_t features)
{
    smbios_cpuid_version = version;
    smbios_cpuid_features = features;
}

#define SMBIOS_SET_DEFAULT(field, value)                                  \
    if (!field) {                                                         \
        field = value;                                                    \
    }

void smbios_set_defaults(const char *manufacturer, const char *product,
                         const char *version, bool legacy_mode,
                         bool uuid_encoded, SmbiosEntryPointType ep_type)
{
    smbios_have_defaults = true;
    smbios_legacy = legacy_mode;
    smbios_uuid_encoded = uuid_encoded;
    smbios_ep_type = ep_type;

    /* drop unwanted version of command-line file blob(s) */
    if (smbios_legacy) {
        g_free(smbios_tables);
        /* in legacy mode, also complain if fields were given for types > 1 */
        if (find_next_bit(have_fields_bitmap,
                          SMBIOS_MAX_TYPE+1, 2) < SMBIOS_MAX_TYPE+1) {
            error_report("can't process fields for smbios "
                         "types > 1 on machine versions < 2.1!");
            exit(1);
        }
    } else {
        g_free(smbios_entries);
    }

    SMBIOS_SET_DEFAULT(type1.manufacturer, manufacturer);
    SMBIOS_SET_DEFAULT(type1.product, product);
    SMBIOS_SET_DEFAULT(type1.version, version);
    SMBIOS_SET_DEFAULT(type2.manufacturer, manufacturer);
    SMBIOS_SET_DEFAULT(type2.product, product);
    SMBIOS_SET_DEFAULT(type2.version, version);
    SMBIOS_SET_DEFAULT(type3.manufacturer, manufacturer);
    SMBIOS_SET_DEFAULT(type3.version, version);
    SMBIOS_SET_DEFAULT(type4.sock_pfx, "CPU");
    SMBIOS_SET_DEFAULT(type4.manufacturer, manufacturer);
    SMBIOS_SET_DEFAULT(type4.version, version);
    SMBIOS_SET_DEFAULT(type17.loc_pfx, "DIMM");
    SMBIOS_SET_DEFAULT(type17.manufacturer, manufacturer);
}

static void smbios_entry_point_setup(void)
{
    switch (smbios_ep_type) {
    case SMBIOS_ENTRY_POINT_TYPE_32:
        memcpy(ep.ep21.anchor_string, "_SM_", 4);
        memcpy(ep.ep21.intermediate_anchor_string, "_DMI_", 5);
        ep.ep21.length = sizeof(struct smbios_21_entry_point);
        ep.ep21.entry_point_revision = 0; /* formatted_area reserved */
        memset(ep.ep21.formatted_area, 0, 5);

        /* compliant with smbios spec v2.8 */
        ep.ep21.smbios_major_version = 2;
        ep.ep21.smbios_minor_version = 8;
        ep.ep21.smbios_bcd_revision = 0x28;

        /* set during table construction, but BIOS may override: */
        ep.ep21.structure_table_length = cpu_to_le16(smbios_tables_len);
        ep.ep21.max_structure_size = cpu_to_le16(smbios_table_max);
        ep.ep21.number_of_structures = cpu_to_le16(smbios_table_cnt);

        /* BIOS must recalculate */
        ep.ep21.checksum = 0;
        ep.ep21.intermediate_checksum = 0;
        ep.ep21.structure_table_address = cpu_to_le32(0);

        break;
    case SMBIOS_ENTRY_POINT_TYPE_64:
        memcpy(ep.ep30.anchor_string, "_SM3_", 5);
        ep.ep30.length = sizeof(struct smbios_30_entry_point);
        ep.ep30.entry_point_revision = 1;
        ep.ep30.reserved = 0;

        /* compliant with smbios spec 3.0 */
        ep.ep30.smbios_major_version = 3;
        ep.ep30.smbios_minor_version = 0;
        ep.ep30.smbios_doc_rev = 0;

        /* set during table construct, but BIOS might override */
        ep.ep30.structure_table_max_size = cpu_to_le32(smbios_tables_len);

        /* BIOS must recalculate */
        ep.ep30.checksum = 0;
        ep.ep30.structure_table_address = cpu_to_le64(0);

        break;
    default:
        abort();
        break;
    }
}

void smbios_get_tables(MachineState *ms,
                       const struct smbios_phys_mem_area *mem_array,
                       const unsigned int mem_array_size,
                       uint8_t **tables, size_t *tables_len,
                       uint8_t **anchor, size_t *anchor_len,
                       Error **errp)
{
    unsigned i, dimm_cnt, offset;

    if (smbios_legacy) {
        *tables = *anchor = NULL;
        *tables_len = *anchor_len = 0;
        return;
    }

    if (!smbios_immutable) {
        smbios_build_type_0_table();
        smbios_build_type_1_table();
        smbios_build_type_2_table();
        smbios_build_type_3_table();

        smbios_smp_sockets = ms->smp.sockets;
        assert(smbios_smp_sockets >= 1);

        for (i = 0; i < smbios_smp_sockets; i++) {
            smbios_build_type_4_table(ms, i);
        }

        smbios_build_type_8_table();
        smbios_build_type_11_table();

#define MAX_DIMM_SZ (16 * GiB)
#define GET_DIMM_SZ ((i < dimm_cnt - 1) ? MAX_DIMM_SZ \
                                        : ((current_machine->ram_size - 1) % MAX_DIMM_SZ) + 1)

        dimm_cnt = QEMU_ALIGN_UP(current_machine->ram_size, MAX_DIMM_SZ) / MAX_DIMM_SZ;

        /*
         * The offset determines if we need to keep additional space between
         * table 17 and table 19 header handle numbers so that they do
         * not overlap. For example, for a VM with larger than 8 TB guest
         * memory and DIMM like chunks of 16 GiB, the default space between
         * the two tables (T19_BASE - T17_BASE = 512) is not enough.
         */
        offset = (dimm_cnt > (T19_BASE - T17_BASE)) ? \
                 dimm_cnt - (T19_BASE - T17_BASE) : 0;

        smbios_build_type_16_table(dimm_cnt);

        for (i = 0; i < dimm_cnt; i++) {
            smbios_build_type_17_table(i, GET_DIMM_SZ);
        }

        for (i = 0; i < mem_array_size; i++) {
            smbios_build_type_19_table(i, offset, mem_array[i].address,
                                       mem_array[i].length);
        }

        /*
         * make sure 16 bit handle numbers in the headers of tables 19
         * and 32 do not overlap.
         */
        assert((mem_array_size + offset) < (T32_BASE - T19_BASE));

        smbios_build_type_32_table();
        smbios_build_type_38_table();
        smbios_build_type_41_table(errp);
        smbios_build_type_127_table();

        smbios_validate_table(ms);
        smbios_entry_point_setup();
        smbios_immutable = true;
    }

    /* return tables blob and entry point (anchor), and their sizes */
    *tables = smbios_tables;
    *tables_len = smbios_tables_len;
    *anchor = (uint8_t *)&ep;

    /* calculate length based on anchor string */
    if (!strncmp((char *)&ep, "_SM_", 4)) {
        *anchor_len = sizeof(struct smbios_21_entry_point);
    } else if (!strncmp((char *)&ep, "_SM3_", 5)) {
        *anchor_len = sizeof(struct smbios_30_entry_point);
    } else {
        abort();
    }
}

static void save_opt(const char **dest, QemuOpts *opts, const char *name)
{
    const char *val = qemu_opt_get(opts, name);

    if (val) {
        *dest = val;
    }
}


struct opt_list {
    size_t *ndest;
    char ***dest;
};

static int save_opt_one(void *opaque,
                        const char *name, const char *value,
                        Error **errp)
{
    struct opt_list *opt = opaque;

    if (g_str_equal(name, "path")) {
        g_autoptr(GByteArray) data = g_byte_array_new();
        g_autofree char *buf = g_new(char, 4096);
        ssize_t ret;
        int fd = qemu_open(value, O_RDONLY, errp);
        if (fd < 0) {
            return -1;
        }

        while (1) {
            ret = read(fd, buf, 4096);
            if (ret == 0) {
                break;
            }
            if (ret < 0) {
                error_setg(errp, "Unable to read from %s: %s",
                           value, strerror(errno));
                qemu_close(fd);
                return -1;
            }
            if (memchr(buf, '\0', ret)) {
                error_setg(errp, "NUL in OEM strings value in %s", value);
                qemu_close(fd);
                return -1;
            }
            g_byte_array_append(data, (guint8 *)buf, ret);
        }

        qemu_close(fd);

        *opt->dest = g_renew(char *, *opt->dest, (*opt->ndest) + 1);
        (*opt->dest)[*opt->ndest] = (char *)g_byte_array_free(data,  FALSE);
        (*opt->ndest)++;
        data = NULL;
   } else if (g_str_equal(name, "value")) {
        *opt->dest = g_renew(char *, *opt->dest, (*opt->ndest) + 1);
        (*opt->dest)[*opt->ndest] = g_strdup(value);
        (*opt->ndest)++;
    } else if (!g_str_equal(name, "type")) {
        error_setg(errp, "Unexpected option %s", name);
        return -1;
    }

    return 0;
}

static bool save_opt_list(size_t *ndest, char ***dest, QemuOpts *opts,
                          Error **errp)
{
    struct opt_list opt = {
        ndest, dest,
    };
    if (!qemu_opt_foreach(opts, save_opt_one, &opt, errp)) {
        return false;
    }
    return true;
}

void smbios_entry_add(QemuOpts *opts, Error **errp)
{
    const char *val;

    assert(!smbios_immutable);

    val = qemu_opt_get(opts, "file");
    if (val) {
        struct smbios_structure_header *header;
        int size;
        struct smbios_table *table; /* legacy mode only */

        if (!qemu_opts_validate(opts, qemu_smbios_file_opts, errp)) {
            return;
        }

        size = get_image_size(val);
        if (size == -1 || size < sizeof(struct smbios_structure_header)) {
            error_setg(errp, "Cannot read SMBIOS file %s", val);
            return;
        }

        /*
         * NOTE: standard double '\0' terminator expected, per smbios spec.
         * (except in legacy mode, where the second '\0' is implicit and
         *  will be inserted by the BIOS).
         */
        smbios_tables = g_realloc(smbios_tables, smbios_tables_len + size);
        header = (struct smbios_structure_header *)(smbios_tables +
                                                    smbios_tables_len);

        if (load_image_size(val, (uint8_t *)header, size) != size) {
            error_setg(errp, "Failed to load SMBIOS file %s", val);
            return;
        }

        if (header->type <= SMBIOS_MAX_TYPE) {
            if (test_bit(header->type, have_fields_bitmap)) {
                error_setg(errp,
                           "can't load type %d struct, fields already specified!",
                           header->type);
                return;
            }
            set_bit(header->type, have_binfile_bitmap);
        }

        if (header->type == 4) {
            smbios_type4_count++;
        }

        smbios_tables_len += size;
        if (size > smbios_table_max) {
            smbios_table_max = size;
        }
        smbios_table_cnt++;

        /* add a copy of the newly loaded blob to legacy smbios_entries */
        /* NOTE: This code runs before smbios_set_defaults(), so we don't
         *       yet know which mode (legacy vs. aggregate-table) will be
         *       required. We therefore add the binary blob to both legacy
         *       (smbios_entries) and aggregate (smbios_tables) tables, and
         *       delete the one we don't need from smbios_set_defaults(),
         *       once we know which machine version has been requested.
         */
        if (!smbios_entries) {
            smbios_entries_len = sizeof(uint16_t);
            smbios_entries = g_malloc0(smbios_entries_len);
        }
        smbios_entries = g_realloc(smbios_entries, smbios_entries_len +
                                                   size + sizeof(*table));
        table = (struct smbios_table *)(smbios_entries + smbios_entries_len);
        table->header.type = SMBIOS_TABLE_ENTRY;
        table->header.length = cpu_to_le16(sizeof(*table) + size);
        memcpy(table->data, header, size);
        smbios_entries_len += sizeof(*table) + size;
        (*(uint16_t *)smbios_entries) =
                cpu_to_le16(le16_to_cpu(*(uint16_t *)smbios_entries) + 1);
        /* end: add a copy of the newly loaded blob to legacy smbios_entries */

        return;
    }

    val = qemu_opt_get(opts, "type");
    if (val) {
        unsigned long type = strtoul(val, NULL, 0);

        if (type > SMBIOS_MAX_TYPE) {
            error_setg(errp, "out of range!");
            return;
        }

        if (test_bit(type, have_binfile_bitmap)) {
            error_setg(errp, "can't add fields, binary file already loaded!");
            return;
        }
        set_bit(type, have_fields_bitmap);

        switch (type) {
        case 0:
            if (!qemu_opts_validate(opts, qemu_smbios_type0_opts, errp)) {
                return;
            }
            save_opt(&type0.vendor, opts, "vendor");
            save_opt(&type0.version, opts, "version");
            save_opt(&type0.date, opts, "date");
            type0.uefi = qemu_opt_get_bool(opts, "uefi", false);

            val = qemu_opt_get(opts, "release");
            if (val) {
                if (sscanf(val, "%hhu.%hhu", &type0.major, &type0.minor) != 2) {
                    error_setg(errp, "Invalid release");
                    return;
                }
                type0.have_major_minor = true;
            }
            return;
        case 1:
            if (!qemu_opts_validate(opts, qemu_smbios_type1_opts, errp)) {
                return;
            }
            save_opt(&type1.manufacturer, opts, "manufacturer");
            save_opt(&type1.product, opts, "product");
            save_opt(&type1.version, opts, "version");
            save_opt(&type1.serial, opts, "serial");
            save_opt(&type1.sku, opts, "sku");
            save_opt(&type1.family, opts, "family");

            val = qemu_opt_get(opts, "uuid");
            if (val) {
                if (qemu_uuid_parse(val, &qemu_uuid) != 0) {
                    error_setg(errp, "Invalid UUID");
                    return;
                }
                qemu_uuid_set = true;
            }
            return;
        case 2:
            if (!qemu_opts_validate(opts, qemu_smbios_type2_opts, errp)) {
                return;
            }
            save_opt(&type2.manufacturer, opts, "manufacturer");
            save_opt(&type2.product, opts, "product");
            save_opt(&type2.version, opts, "version");
            save_opt(&type2.serial, opts, "serial");
            save_opt(&type2.asset, opts, "asset");
            save_opt(&type2.location, opts, "location");
            return;
        case 3:
            if (!qemu_opts_validate(opts, qemu_smbios_type3_opts, errp)) {
                return;
            }
            save_opt(&type3.manufacturer, opts, "manufacturer");
            save_opt(&type3.version, opts, "version");
            save_opt(&type3.serial, opts, "serial");
            save_opt(&type3.asset, opts, "asset");
            save_opt(&type3.sku, opts, "sku");
            return;
        case 4:
            if (!qemu_opts_validate(opts, qemu_smbios_type4_opts, errp)) {
                return;
            }
            save_opt(&type4.sock_pfx, opts, "sock_pfx");
            save_opt(&type4.manufacturer, opts, "manufacturer");
            save_opt(&type4.version, opts, "version");
            save_opt(&type4.serial, opts, "serial");
            save_opt(&type4.asset, opts, "asset");
            save_opt(&type4.part, opts, "part");
            /* If the value is 0, it will take the value from the CPU model. */
            type4.processor_id = qemu_opt_get_number(opts, "processor-id", 0);
            type4.max_speed = qemu_opt_get_number(opts, "max-speed",
                                                  DEFAULT_CPU_SPEED);
            type4.current_speed = qemu_opt_get_number(opts, "current-speed",
                                                      DEFAULT_CPU_SPEED);
            if (type4.max_speed > UINT16_MAX ||
                type4.current_speed > UINT16_MAX) {
                error_setg(errp, "SMBIOS CPU speed is too large (> %d)",
                           UINT16_MAX);
            }
            return;
        case 8:
            if (!qemu_opts_validate(opts, qemu_smbios_type8_opts, errp)) {
                return;
            }
            struct type8_instance *t8_i;
            t8_i = g_new0(struct type8_instance, 1);
            save_opt(&t8_i->internal_reference, opts, "internal_reference");
            save_opt(&t8_i->external_reference, opts, "external_reference");
            t8_i->connector_type = qemu_opt_get_number(opts,
                                                       "connector_type", 0);
            t8_i->port_type = qemu_opt_get_number(opts, "port_type", 0);
            QTAILQ_INSERT_TAIL(&type8, t8_i, next);
            return;
        case 11:
            if (!qemu_opts_validate(opts, qemu_smbios_type11_opts, errp)) {
                return;
            }
            if (!save_opt_list(&type11.nvalues, &type11.values, opts, errp)) {
                return;
            }
            return;
        case 17:
            if (!qemu_opts_validate(opts, qemu_smbios_type17_opts, errp)) {
                return;
            }
            save_opt(&type17.loc_pfx, opts, "loc_pfx");
            save_opt(&type17.bank, opts, "bank");
            save_opt(&type17.manufacturer, opts, "manufacturer");
            save_opt(&type17.serial, opts, "serial");
            save_opt(&type17.asset, opts, "asset");
            save_opt(&type17.part, opts, "part");
            type17.speed = qemu_opt_get_number(opts, "speed", 0);
            return;
        case 41: {
            struct type41_instance *t41_i;
            Error *local_err = NULL;

            if (!qemu_opts_validate(opts, qemu_smbios_type41_opts, errp)) {
                return;
            }
            t41_i = g_new0(struct type41_instance, 1);
            save_opt(&t41_i->designation, opts, "designation");
            t41_i->kind = qapi_enum_parse(&type41_kind_lookup,
                                          qemu_opt_get(opts, "kind"),
                                          0, &local_err) + 1;
            t41_i->kind |= 0x80;     /* enabled */
            if (local_err != NULL) {
                error_propagate(errp, local_err);
                g_free(t41_i);
                return;
            }
            t41_i->instance = qemu_opt_get_number(opts, "instance", 1);
            save_opt(&t41_i->pcidev, opts, "pcidev");

            QTAILQ_INSERT_TAIL(&type41, t41_i, next);
            return;
        }
        default:
            error_setg(errp,
                       "Don't know how to build fields for SMBIOS type %ld",
                       type);
            return;
        }
    }

    error_setg(errp, "Must specify type= or file=");
}
