/*
 * Boot order test cases.
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "qemu-common.h"
#include "libqtest.h"
#include "qemu/compiler.h"
#include "hw/i386/acpi-defs.h"
#include "hw/i386/smbios.h"
#include "qemu/bitmap.h"

#define MACHINE_PC "pc"
#define MACHINE_Q35 "q35"

#define ACPI_REBUILD_EXPECTED_AML "TEST_ACPI_REBUILD_AML"

/* DSDT and SSDTs format */
typedef struct {
    AcpiTableHeader header;
    gchar *aml;            /* aml bytecode from guest */
    gsize aml_len;
    gchar *aml_file;
    gchar *asl;            /* asl code generated from aml */
    gsize asl_len;
    gchar *asl_file;
    bool tmp_files_retain;   /* do not delete the temp asl/aml */
} QEMU_PACKED AcpiSdtTable;

typedef struct {
    const char *machine;
    uint32_t rsdp_addr;
    AcpiRsdpDescriptor rsdp_table;
    AcpiRsdtDescriptorRev1 rsdt_table;
    AcpiFadtDescriptorRev1 fadt_table;
    AcpiFacsDescriptorRev1 facs_table;
    uint32_t *rsdt_tables_addr;
    int rsdt_tables_nr;
    GArray *tables;
    uint32_t smbios_ep_addr;
    struct smbios_entry_point smbios_ep_table;
} test_data;

#define LOW(x) ((x) & 0xff)
#define HIGH(x) ((x) >> 8)

#define SIGNATURE 0xdead
#define SIGNATURE_OFFSET 0x10
#define BOOT_SECTOR_ADDRESS 0x7c00

#define ACPI_READ_FIELD(field, addr)           \
    do {                                       \
        switch (sizeof(field)) {               \
        case 1:                                \
            field = readb(addr);               \
            break;                             \
        case 2:                                \
            field = readw(addr);               \
            break;                             \
        case 4:                                \
            field = readl(addr);               \
            break;                             \
        case 8:                                \
            field = readq(addr);               \
            break;                             \
        default:                               \
            g_assert(false);                   \
        }                                      \
        addr += sizeof(field);                  \
    } while (0);

#define ACPI_READ_ARRAY_PTR(arr, length, addr)  \
    do {                                        \
        int idx;                                \
        for (idx = 0; idx < length; ++idx) {    \
            ACPI_READ_FIELD(arr[idx], addr);    \
        }                                       \
    } while (0);

#define ACPI_READ_ARRAY(arr, addr)                               \
    ACPI_READ_ARRAY_PTR(arr, sizeof(arr)/sizeof(arr[0]), addr)

#define ACPI_READ_TABLE_HEADER(table, addr)                      \
    do {                                                         \
        ACPI_READ_FIELD((table)->signature, addr);               \
        ACPI_READ_FIELD((table)->length, addr);                  \
        ACPI_READ_FIELD((table)->revision, addr);                \
        ACPI_READ_FIELD((table)->checksum, addr);                \
        ACPI_READ_ARRAY((table)->oem_id, addr);                  \
        ACPI_READ_ARRAY((table)->oem_table_id, addr);            \
        ACPI_READ_FIELD((table)->oem_revision, addr);            \
        ACPI_READ_ARRAY((table)->asl_compiler_id, addr);         \
        ACPI_READ_FIELD((table)->asl_compiler_revision, addr);   \
    } while (0);

#define ACPI_ASSERT_CMP(actual, expected) do { \
    uint32_t ACPI_ASSERT_CMP_le = cpu_to_le32(actual); \
    char ACPI_ASSERT_CMP_str[5] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &ACPI_ASSERT_CMP_le, 4); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)

#define ACPI_ASSERT_CMP64(actual, expected) do { \
    uint64_t ACPI_ASSERT_CMP_le = cpu_to_le64(actual); \
    char ACPI_ASSERT_CMP_str[9] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &ACPI_ASSERT_CMP_le, 8); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)

/* Boot sector code: write SIGNATURE into memory,
 * then halt.
 * Q35 machine requires a minimum 0x7e000 bytes disk.
 * (bug or feature?)
 */
static uint8_t boot_sector[0x7e000] = {
    /* 7c00: mov $0xdead,%ax */
    [0x00] = 0xb8,
    [0x01] = LOW(SIGNATURE),
    [0x02] = HIGH(SIGNATURE),
    /* 7c03:  mov %ax,0x7c10 */
    [0x03] = 0xa3,
    [0x04] = LOW(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET),
    [0x05] = HIGH(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET),
    /* 7c06: cli */
    [0x06] = 0xfa,
    /* 7c07: hlt */
    [0x07] = 0xf4,
    /* 7c08: jmp 0x7c07=0x7c0a-3 */
    [0x08] = 0xeb,
    [0x09] = LOW(-3),
    /* We mov 0xdead here: set value to make debugging easier */
    [SIGNATURE_OFFSET] = LOW(0xface),
    [SIGNATURE_OFFSET + 1] = HIGH(0xface),
    /* End of boot sector marker */
    [0x1FE] = 0x55,
    [0x1FF] = 0xAA,
};

static const char *disk = "tests/acpi-test-disk.raw";
static const char *data_dir = "tests/acpi-test-data";
#ifdef CONFIG_IASL
static const char *iasl = stringify(CONFIG_IASL);
#else
static const char *iasl;
#endif

static void free_test_data(test_data *data)
{
    AcpiSdtTable *temp;
    int i;

    if (data->rsdt_tables_addr) {
        g_free(data->rsdt_tables_addr);
    }

    for (i = 0; i < data->tables->len; ++i) {
        temp = &g_array_index(data->tables, AcpiSdtTable, i);
        if (temp->aml) {
            g_free(temp->aml);
        }
        if (temp->aml_file) {
            if (!temp->tmp_files_retain &&
                g_strstr_len(temp->aml_file, -1, "aml-")) {
                unlink(temp->aml_file);
            }
            g_free(temp->aml_file);
        }
        if (temp->asl) {
            g_free(temp->asl);
        }
        if (temp->asl_file) {
            if (!temp->tmp_files_retain) {
                unlink(temp->asl_file);
            }
            g_free(temp->asl_file);
        }
    }

    g_array_free(data->tables, false);
}

static uint8_t acpi_checksum(const uint8_t *data, int len)
{
    int i;
    uint8_t sum = 0;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }

    return sum;
}

static void test_acpi_rsdp_address(test_data *data)
{
    uint32_t off;

    /* OK, now find RSDP */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "RSD PTR ";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = readb(off + i);
        }

        if (!memcmp(sig, "RSD PTR ", sizeof sig)) {
            break;
        }
    }

    g_assert_cmphex(off, <, 0x100000);
    data->rsdp_addr = off;
}

static void test_acpi_rsdp_table(test_data *data)
{
    AcpiRsdpDescriptor *rsdp_table = &data->rsdp_table;
    uint32_t addr = data->rsdp_addr;

    ACPI_READ_FIELD(rsdp_table->signature, addr);
    ACPI_ASSERT_CMP64(rsdp_table->signature, "RSD PTR ");

    ACPI_READ_FIELD(rsdp_table->checksum, addr);
    ACPI_READ_ARRAY(rsdp_table->oem_id, addr);
    ACPI_READ_FIELD(rsdp_table->revision, addr);
    ACPI_READ_FIELD(rsdp_table->rsdt_physical_address, addr);
    ACPI_READ_FIELD(rsdp_table->length, addr);

    /* rsdp checksum is not for the whole table, but for the first 20 bytes */
    g_assert(!acpi_checksum((uint8_t *)rsdp_table, 20));
}

static void test_acpi_rsdt_table(test_data *data)
{
    AcpiRsdtDescriptorRev1 *rsdt_table = &data->rsdt_table;
    uint32_t addr = data->rsdp_table.rsdt_physical_address;
    uint32_t *tables;
    int tables_nr;
    uint8_t checksum;

    /* read the header */
    ACPI_READ_TABLE_HEADER(rsdt_table, addr);
    ACPI_ASSERT_CMP(rsdt_table->signature, "RSDT");

    /* compute the table entries in rsdt */
    tables_nr = (rsdt_table->length - sizeof(AcpiRsdtDescriptorRev1)) /
                sizeof(uint32_t);
    g_assert_cmpint(tables_nr, >, 0);

    /* get the addresses of the tables pointed by rsdt */
    tables = g_new0(uint32_t, tables_nr);
    ACPI_READ_ARRAY_PTR(tables, tables_nr, addr);

    checksum = acpi_checksum((uint8_t *)rsdt_table, rsdt_table->length) +
               acpi_checksum((uint8_t *)tables, tables_nr * sizeof(uint32_t));
    g_assert(!checksum);

   /* SSDT tables after FADT */
    data->rsdt_tables_addr = tables;
    data->rsdt_tables_nr = tables_nr;
}

static void test_acpi_fadt_table(test_data *data)
{
    AcpiFadtDescriptorRev1 *fadt_table = &data->fadt_table;
    uint32_t addr;

    /* FADT table comes first */
    addr = data->rsdt_tables_addr[0];
    ACPI_READ_TABLE_HEADER(fadt_table, addr);

    ACPI_READ_FIELD(fadt_table->firmware_ctrl, addr);
    ACPI_READ_FIELD(fadt_table->dsdt, addr);
    ACPI_READ_FIELD(fadt_table->model, addr);
    ACPI_READ_FIELD(fadt_table->reserved1, addr);
    ACPI_READ_FIELD(fadt_table->sci_int, addr);
    ACPI_READ_FIELD(fadt_table->smi_cmd, addr);
    ACPI_READ_FIELD(fadt_table->acpi_enable, addr);
    ACPI_READ_FIELD(fadt_table->acpi_disable, addr);
    ACPI_READ_FIELD(fadt_table->S4bios_req, addr);
    ACPI_READ_FIELD(fadt_table->reserved2, addr);
    ACPI_READ_FIELD(fadt_table->pm1a_evt_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm1b_evt_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm1a_cnt_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm1b_cnt_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm2_cnt_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm_tmr_blk, addr);
    ACPI_READ_FIELD(fadt_table->gpe0_blk, addr);
    ACPI_READ_FIELD(fadt_table->gpe1_blk, addr);
    ACPI_READ_FIELD(fadt_table->pm1_evt_len, addr);
    ACPI_READ_FIELD(fadt_table->pm1_cnt_len, addr);
    ACPI_READ_FIELD(fadt_table->pm2_cnt_len, addr);
    ACPI_READ_FIELD(fadt_table->pm_tmr_len, addr);
    ACPI_READ_FIELD(fadt_table->gpe0_blk_len, addr);
    ACPI_READ_FIELD(fadt_table->gpe1_blk_len, addr);
    ACPI_READ_FIELD(fadt_table->gpe1_base, addr);
    ACPI_READ_FIELD(fadt_table->reserved3, addr);
    ACPI_READ_FIELD(fadt_table->plvl2_lat, addr);
    ACPI_READ_FIELD(fadt_table->plvl3_lat, addr);
    ACPI_READ_FIELD(fadt_table->flush_size, addr);
    ACPI_READ_FIELD(fadt_table->flush_stride, addr);
    ACPI_READ_FIELD(fadt_table->duty_offset, addr);
    ACPI_READ_FIELD(fadt_table->duty_width, addr);
    ACPI_READ_FIELD(fadt_table->day_alrm, addr);
    ACPI_READ_FIELD(fadt_table->mon_alrm, addr);
    ACPI_READ_FIELD(fadt_table->century, addr);
    ACPI_READ_FIELD(fadt_table->reserved4, addr);
    ACPI_READ_FIELD(fadt_table->reserved4a, addr);
    ACPI_READ_FIELD(fadt_table->reserved4b, addr);
    ACPI_READ_FIELD(fadt_table->flags, addr);

    ACPI_ASSERT_CMP(fadt_table->signature, "FACP");
    g_assert(!acpi_checksum((uint8_t *)fadt_table, fadt_table->length));
}

static void test_acpi_facs_table(test_data *data)
{
    AcpiFacsDescriptorRev1 *facs_table = &data->facs_table;
    uint32_t addr = data->fadt_table.firmware_ctrl;

    ACPI_READ_FIELD(facs_table->signature, addr);
    ACPI_READ_FIELD(facs_table->length, addr);
    ACPI_READ_FIELD(facs_table->hardware_signature, addr);
    ACPI_READ_FIELD(facs_table->firmware_waking_vector, addr);
    ACPI_READ_FIELD(facs_table->global_lock, addr);
    ACPI_READ_FIELD(facs_table->flags, addr);
    ACPI_READ_ARRAY(facs_table->resverved3, addr);

    ACPI_ASSERT_CMP(facs_table->signature, "FACS");
}

static void test_dst_table(AcpiSdtTable *sdt_table, uint32_t addr)
{
    uint8_t checksum;

    ACPI_READ_TABLE_HEADER(&sdt_table->header, addr);

    sdt_table->aml_len = sdt_table->header.length - sizeof(AcpiTableHeader);
    sdt_table->aml = g_malloc0(sdt_table->aml_len);
    ACPI_READ_ARRAY_PTR(sdt_table->aml, sdt_table->aml_len, addr);

    checksum = acpi_checksum((uint8_t *)sdt_table, sizeof(AcpiTableHeader)) +
               acpi_checksum((uint8_t *)sdt_table->aml, sdt_table->aml_len);
    g_assert(!checksum);
}

static void test_acpi_dsdt_table(test_data *data)
{
    AcpiSdtTable dsdt_table;
    uint32_t addr = data->fadt_table.dsdt;

    memset(&dsdt_table, 0, sizeof(dsdt_table));
    data->tables = g_array_new(false, true, sizeof(AcpiSdtTable));

    test_dst_table(&dsdt_table, addr);
    ACPI_ASSERT_CMP(dsdt_table.header.signature, "DSDT");

    /* Place DSDT first */
    g_array_append_val(data->tables, dsdt_table);
}

static void test_acpi_tables(test_data *data)
{
    int tables_nr = data->rsdt_tables_nr - 1; /* fadt is first */
    int i;

    for (i = 0; i < tables_nr; i++) {
        AcpiSdtTable ssdt_table;

        memset(&ssdt_table, 0 , sizeof(ssdt_table));
        uint32_t addr = data->rsdt_tables_addr[i + 1]; /* fadt is first */
        test_dst_table(&ssdt_table, addr);
        g_array_append_val(data->tables, ssdt_table);
    }
}

static void dump_aml_files(test_data *data, bool rebuild)
{
    AcpiSdtTable *sdt;
    GError *error = NULL;
    gchar *aml_file = NULL;
    gint fd;
    ssize_t ret;
    int i;

    for (i = 0; i < data->tables->len; ++i) {
        sdt = &g_array_index(data->tables, AcpiSdtTable, i);
        g_assert(sdt->aml);

        if (rebuild) {
            uint32_t signature = cpu_to_le32(sdt->header.signature);
            aml_file = g_strdup_printf("%s/%s/%.4s", data_dir, data->machine,
                                       (gchar *)&signature);
            fd = g_open(aml_file, O_WRONLY|O_TRUNC|O_CREAT,
                        S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
        } else {
            fd = g_file_open_tmp("aml-XXXXXX", &sdt->aml_file, &error);
            g_assert_no_error(error);
        }
        g_assert(fd >= 0);

        ret = qemu_write_full(fd, sdt, sizeof(AcpiTableHeader));
        g_assert(ret == sizeof(AcpiTableHeader));
        ret = qemu_write_full(fd, sdt->aml, sdt->aml_len);
        g_assert(ret == sdt->aml_len);

        close(fd);

        if (aml_file) {
            g_free(aml_file);
        }
    }
}

static bool compare_signature(AcpiSdtTable *sdt, const char *signature)
{
   return !memcmp(&sdt->header.signature, signature, 4);
}

static bool load_asl(GArray *sdts, AcpiSdtTable *sdt)
{
    AcpiSdtTable *temp;
    GError *error = NULL;
    GString *command_line = g_string_new(iasl);
    gint fd;
    gchar *out, *out_err;
    gboolean ret;
    int i;

    fd = g_file_open_tmp("asl-XXXXXX.dsl", &sdt->asl_file, &error);
    g_assert_no_error(error);
    close(fd);

    /* build command line */
    g_string_append_printf(command_line, " -p %s ", sdt->asl_file);
    if (compare_signature(sdt, "DSDT") ||
        compare_signature(sdt, "SSDT")) {
        for (i = 0; i < sdts->len; ++i) {
            temp = &g_array_index(sdts, AcpiSdtTable, i);
            if (compare_signature(temp, "DSDT") ||
                compare_signature(temp, "SSDT")) {
                g_string_append_printf(command_line, "-e %s ", temp->aml_file);
            }
        }
    }
    g_string_append_printf(command_line, "-d %s", sdt->aml_file);

    /* pass 'out' and 'out_err' in order to be redirected */
    ret = g_spawn_command_line_sync(command_line->str, &out, &out_err, NULL, &error);
    g_assert_no_error(error);
    if (ret) {
        ret = g_file_get_contents(sdt->asl_file, (gchar **)&sdt->asl,
                                  &sdt->asl_len, &error);
        g_assert(ret);
        g_assert_no_error(error);
        ret = (sdt->asl_len > 0);
    }

    g_free(out);
    g_free(out_err);
    g_string_free(command_line, true);

    return !ret;
}

#define COMMENT_END "*/"
#define DEF_BLOCK "DefinitionBlock ("
#define BLOCK_NAME_END ".aml"

static GString *normalize_asl(gchar *asl_code)
{
    GString *asl = g_string_new(asl_code);
    gchar *comment, *block_name;

    /* strip comments (different generation days) */
    comment = g_strstr_len(asl->str, asl->len, COMMENT_END);
    if (comment) {
        comment += strlen(COMMENT_END);
        while (*comment == '\n') {
            comment++;
        }
        asl = g_string_erase(asl, 0, comment - asl->str);
    }

    /* strip def block name (it has file path in it) */
    if (g_str_has_prefix(asl->str, DEF_BLOCK)) {
        block_name = g_strstr_len(asl->str, asl->len, BLOCK_NAME_END);
        g_assert(block_name);
        asl = g_string_erase(asl, 0,
                             block_name + sizeof(BLOCK_NAME_END) - asl->str);
    }

    return asl;
}

static GArray *load_expected_aml(test_data *data)
{
    int i;
    AcpiSdtTable *sdt;
    gchar *aml_file;
    GError *error = NULL;
    gboolean ret;

    GArray *exp_tables = g_array_new(false, true, sizeof(AcpiSdtTable));
    for (i = 0; i < data->tables->len; ++i) {
        AcpiSdtTable exp_sdt;
        uint32_t signature;

        sdt = &g_array_index(data->tables, AcpiSdtTable, i);

        memset(&exp_sdt, 0, sizeof(exp_sdt));
        exp_sdt.header.signature = sdt->header.signature;

        signature = cpu_to_le32(sdt->header.signature);
        aml_file = g_strdup_printf("%s/%s/%.4s", data_dir, data->machine,
                                   (gchar *)&signature);
        exp_sdt.aml_file = aml_file;
        g_assert(g_file_test(aml_file, G_FILE_TEST_EXISTS));
        ret = g_file_get_contents(aml_file, &exp_sdt.aml,
                                  &exp_sdt.aml_len, &error);
        g_assert(ret);
        g_assert_no_error(error);
        g_assert(exp_sdt.aml);
        g_assert(exp_sdt.aml_len);

        g_array_append_val(exp_tables, exp_sdt);
    }

    return exp_tables;
}

static void test_acpi_asl(test_data *data)
{
    int i;
    AcpiSdtTable *sdt, *exp_sdt;
    test_data exp_data;
    gboolean exp_err, err;

    memset(&exp_data, 0, sizeof(exp_data));
    exp_data.tables = load_expected_aml(data);
    dump_aml_files(data, false);
    for (i = 0; i < data->tables->len; ++i) {
        GString *asl, *exp_asl;

        sdt = &g_array_index(data->tables, AcpiSdtTable, i);
        exp_sdt = &g_array_index(exp_data.tables, AcpiSdtTable, i);

        err = load_asl(data->tables, sdt);
        asl = normalize_asl(sdt->asl);

        exp_err = load_asl(exp_data.tables, exp_sdt);
        exp_asl = normalize_asl(exp_sdt->asl);

        /* TODO: check for warnings */
        g_assert(!err || exp_err);

        if (g_strcmp0(asl->str, exp_asl->str)) {
            if (exp_err) {
                fprintf(stderr,
                        "Warning! iasl couldn't parse the expected aml\n");
            } else {
                uint32_t signature = cpu_to_le32(exp_sdt->header.signature);
                sdt->tmp_files_retain = true;
                exp_sdt->tmp_files_retain = true;
                fprintf(stderr,
                        "acpi-test: Warning! %.4s mismatch. "
                        "Actual [asl:%s, aml:%s], Expected [asl:%s, aml:%s].\n",
                        (gchar *)&signature,
                        sdt->asl_file, sdt->aml_file,
                        exp_sdt->asl_file, exp_sdt->aml_file);
          }
        }
        g_string_free(asl, true);
        g_string_free(exp_asl, true);
    }

    free_test_data(&exp_data);
}

static void test_smbios_ep_address(test_data *data)
{
    uint32_t off;

    /* find smbios entry point structure */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "_SM_";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = readb(off + i);
        }

        if (!memcmp(sig, "_SM_", sizeof sig)) {
            break;
        }
    }

    g_assert_cmphex(off, <, 0x100000);
    data->smbios_ep_addr = off;
}

static void test_smbios_ep_table(test_data *data)
{
    struct smbios_entry_point *ep_table = &data->smbios_ep_table;
    uint32_t addr = data->smbios_ep_addr;

    ACPI_READ_ARRAY(ep_table->anchor_string, addr);
    g_assert(!memcmp(ep_table->anchor_string, "_SM_", 4));
    ACPI_READ_FIELD(ep_table->checksum, addr);
    ACPI_READ_FIELD(ep_table->length, addr);
    ACPI_READ_FIELD(ep_table->smbios_major_version, addr);
    ACPI_READ_FIELD(ep_table->smbios_minor_version, addr);
    ACPI_READ_FIELD(ep_table->max_structure_size, addr);
    ACPI_READ_FIELD(ep_table->entry_point_revision, addr);
    ACPI_READ_ARRAY(ep_table->formatted_area, addr);
    ACPI_READ_ARRAY(ep_table->intermediate_anchor_string, addr);
    g_assert(!memcmp(ep_table->intermediate_anchor_string, "_DMI_", 5));
    ACPI_READ_FIELD(ep_table->intermediate_checksum, addr);
    ACPI_READ_FIELD(ep_table->structure_table_length, addr);
    g_assert_cmpuint(ep_table->structure_table_length, >, 0);
    ACPI_READ_FIELD(ep_table->structure_table_address, addr);
    ACPI_READ_FIELD(ep_table->number_of_structures, addr);
    g_assert_cmpuint(ep_table->number_of_structures, >, 0);
    ACPI_READ_FIELD(ep_table->smbios_bcd_revision, addr);
    g_assert(!acpi_checksum((uint8_t *)ep_table, sizeof *ep_table));
    g_assert(!acpi_checksum((uint8_t *)ep_table + 0x10,
                            sizeof *ep_table - 0x10));
}

static inline bool smbios_single_instance(uint8_t type)
{
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 16:
    case 32:
    case 127:
        return true;
    default:
        return false;
    }
}

static void test_smbios_structs(test_data *data)
{
    DECLARE_BITMAP(struct_bitmap, SMBIOS_MAX_TYPE+1) = { 0 };
    struct smbios_entry_point *ep_table = &data->smbios_ep_table;
    uint32_t addr = ep_table->structure_table_address;
    int i, len, max_len = 0;
    uint8_t type, prv, crt;
    uint8_t required_struct_types[] = {0, 1, 3, 4, 16, 17, 19, 32, 127};

    /* walk the smbios tables */
    for (i = 0; i < ep_table->number_of_structures; i++) {

        /* grab type and formatted area length from struct header */
        type = readb(addr);
        g_assert_cmpuint(type, <=, SMBIOS_MAX_TYPE);
        len = readb(addr + 1);

        /* single-instance structs must not have been encountered before */
        if (smbios_single_instance(type)) {
            g_assert(!test_bit(type, struct_bitmap));
        }
        set_bit(type, struct_bitmap);

        /* seek to end of unformatted string area of this struct ("\0\0") */
        prv = crt = 1;
        while (prv || crt) {
            prv = crt;
            crt = readb(addr + len);
            len++;
        }

        /* keep track of max. struct size */
        if (max_len < len) {
            max_len = len;
            g_assert_cmpuint(max_len, <=, ep_table->max_structure_size);
        }

        /* start of next structure */
        addr += len;
    }

    /* total table length and max struct size must match entry point values */
    g_assert_cmpuint(ep_table->structure_table_length, ==,
                     addr - ep_table->structure_table_address);
    g_assert_cmpuint(ep_table->max_structure_size, ==, max_len);

    /* required struct types must all be present */
    for (i = 0; i < ARRAY_SIZE(required_struct_types); i++) {
        g_assert(test_bit(required_struct_types[i], struct_bitmap));
    }
}

static void test_acpi_one(const char *params, test_data *data)
{
    char *args;
    uint8_t signature_low;
    uint8_t signature_high;
    uint16_t signature;
    int i;

    args = g_strdup_printf("-net none -display none %s "
                           "-drive id=hd0,if=none,file=%s "
                           "-device ide-hd,drive=hd0 ",
                           params ? params : "", disk);

    qtest_start(args);

   /* Wait at most 1 minute */
#define TEST_DELAY (1 * G_USEC_PER_SEC / 10)
#define TEST_CYCLES MAX((60 * G_USEC_PER_SEC / TEST_DELAY), 1)

    /* Poll until code has run and modified memory.  Once it has we know BIOS
     * initialization is done.  TODO: check that IP reached the halt
     * instruction.
     */
    for (i = 0; i < TEST_CYCLES; ++i) {
        signature_low = readb(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET);
        signature_high = readb(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET + 1);
        signature = (signature_high << 8) | signature_low;
        if (signature == SIGNATURE) {
            break;
        }
        g_usleep(TEST_DELAY);
    }
    g_assert_cmphex(signature, ==, SIGNATURE);

    test_acpi_rsdp_address(data);
    test_acpi_rsdp_table(data);
    test_acpi_rsdt_table(data);
    test_acpi_fadt_table(data);
    test_acpi_facs_table(data);
    test_acpi_dsdt_table(data);
    test_acpi_tables(data);

    if (iasl) {
        if (getenv(ACPI_REBUILD_EXPECTED_AML)) {
            dump_aml_files(data, true);
        } else {
            test_acpi_asl(data);
        }
    }

    test_smbios_ep_address(data);
    test_smbios_ep_table(data);
    test_smbios_structs(data);

    qtest_quit(global_qtest);
    g_free(args);
}

static void test_acpi_tcg(void)
{
    test_data data;

    /* Supplying -machine accel argument overrides the default (qtest).
     * This is to make guest actually run.
     */
    memset(&data, 0, sizeof(data));
    data.machine = MACHINE_PC;
    test_acpi_one("-machine accel=tcg", &data);
    free_test_data(&data);

    memset(&data, 0, sizeof(data));
    data.machine = MACHINE_Q35;
    test_acpi_one("-machine q35,accel=tcg", &data);
    free_test_data(&data);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    FILE *f = fopen(disk, "w");
    int ret;

    if (!f) {
        fprintf(stderr, "Couldn't open \"%s\": %s", disk, strerror(errno));
        return 1;
    }
    fwrite(boot_sector, 1, sizeof boot_sector, f);
    fclose(f);

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("acpi/tcg", test_acpi_tcg);
    }
    ret = g_test_run();
    unlink(disk);
    return ret;
}
