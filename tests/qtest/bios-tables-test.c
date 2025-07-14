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

/*
 * How to add or update the tests or commit changes that affect ACPI tables:
 * Contributor:
 * 1. add empty files for new tables, if any, under tests/data/acpi
 * 2. list any changed files in tests/qtest/bios-tables-test-allowed-diff.h
 * 3. commit the above *before* making changes that affect the tables
 *
 * Contributor or ACPI Maintainer (steps 4-7 need to be redone to resolve conflicts
 * in binary commit created in step 6):
 *
 * After 1-3 above tests will pass but ignore differences with the expected files.
 * You will also notice that tests/qtest/bios-tables-test-allowed-diff.h lists
 * a bunch of files. This is your hint that you need to do the below:
 * 4. Run
 *      make check V=2
 * this will produce a bunch of warnings about differences
 * between actual and expected ACPI tables. If you have IASL installed,
 * they will also be disassembled so you can look at the disassembled
 * output. If not - disassemble them yourself in any way you like.
 * Look at the differences - make sure they make sense and match what the
 * changes you are merging are supposed to do.
 * Save the changes, preferably in form of ASL diff for the commit log in
 * step 6.
 *
 * 5. From build directory, run:
 *      $(SRC_PATH)/tests/data/acpi/rebuild-expected-aml.sh
 * 6. Now commit any changes to the expected binary, include diff from step 4
 *    in commit log.
 *    Expected binary updates needs to be a separate patch from the code that
 *    introduces changes to ACPI tables. It lets the maintainer drop
 *    and regenerate binary updates in case of merge conflicts. Further, a code
 *    change is easily reviewable but a binary blob is not (without doing a
 *    disassembly).
 * 7. Before sending patches to the list (Contributor)
 *    or before doing a pull request (Maintainer), make sure
 *    tests/qtest/bios-tables-test-allowed-diff.h is empty - this will ensure
 *    following changes to ACPI tables will be noticed.
 *
 * The resulting patchset/pull request then looks like this:
 * - patch 1: list changed files in tests/qtest/bios-tables-test-allowed-diff.h.
 * - patches 2 - n: real changes, may contain multiple patches.
 * - patch n + 1: update golden master binaries and empty
 *   tests/qtest/bios-tables-test-allowed-diff.h
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "hw/firmware/smbios.h"
#include "qemu/bitmap.h"
#include "acpi-utils.h"
#include "boot-sector.h"
#include "tpm-emu.h"
#include "hw/acpi/tpm.h"
#include "qemu/cutils.h"

#define MACHINE_PC "pc"
#define MACHINE_Q35 "q35"

#define ACPI_REBUILD_EXPECTED_AML "TEST_ACPI_REBUILD_AML"

#define OEM_ID             "TEST"
#define OEM_TABLE_ID       "OEM"
#define OEM_TEST_ARGS      "-machine x-oem-id=" OEM_ID ",x-oem-table-id=" \
                           OEM_TABLE_ID

typedef struct {
    bool tcg_only;
    const char *machine;
    const char *arch;
    const char *machine_param;
    const char *variant;
    const char *uefi_fl1;
    const char *uefi_fl2;
    const char *blkdev;
    const char *cd;
    const uint64_t ram_start;
    const uint64_t scan_len;
    uint64_t rsdp_addr;
    uint8_t rsdp_table[36 /* ACPI 2.0+ RSDP size */];
    GArray *tables;
    uint64_t smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE__MAX];
    SmbiosEntryPoint smbios_ep_table;
    uint16_t smbios_cpu_max_speed;
    uint16_t smbios_cpu_curr_speed;
    uint8_t smbios_core_count;
    uint16_t smbios_core_count2;
    uint8_t smbios_thread_count;
    uint16_t smbios_thread_count2;
    uint8_t *required_struct_types;
    int required_struct_types_len;
    int type4_count;
    QTestState *qts;
} test_data;

static char disk[] = "tests/acpi-test-disk-XXXXXX";
static const char *data_dir = "tests/data/acpi";
#ifdef CONFIG_IASL
static const char *iasl = CONFIG_IASL;
#else
static const char *iasl;
#endif

static int verbosity_level;
static GArray *load_expected_aml(test_data *data);

static bool compare_signature(const AcpiSdtTable *sdt, const char *signature)
{
   return !memcmp(sdt->aml, signature, 4);
}

static void cleanup_table_descriptor(AcpiSdtTable *table)
{
    g_free(table->aml);
    if (table->aml_file &&
        !table->tmp_files_retain &&
        g_strstr_len(table->aml_file, -1, "aml-")) {
        unlink(table->aml_file);
    }
    g_free(table->aml_file);
    g_free(table->asl);
    if (table->asl_file &&
        !table->tmp_files_retain) {
        unlink(table->asl_file);
    }
    g_free(table->asl_file);
}

static void free_test_data(test_data *data)
{
    int i;

    if (!data->tables) {
        return;
    }
    for (i = 0; i < data->tables->len; ++i) {
        cleanup_table_descriptor(&g_array_index(data->tables, AcpiSdtTable, i));
    }

    g_array_free(data->tables, true);
}

static void test_acpi_rsdp_table(test_data *data)
{
    uint8_t *rsdp_table = data->rsdp_table;

    acpi_fetch_rsdp_table(data->qts, data->rsdp_addr, rsdp_table);

    switch (rsdp_table[15 /* Revision offset */]) {
    case 0: /* ACPI 1.0 RSDP */
        /* With rev 1, checksum is only for the first 20 bytes */
        g_assert(!acpi_calc_checksum(rsdp_table,  20));
        break;
    case 2: /* ACPI 2.0+ RSDP */
        /* With revision 2, we have 2 checksums */
        g_assert(!acpi_calc_checksum(rsdp_table, 20));
        g_assert(!acpi_calc_checksum(rsdp_table, 36));
        break;
    default:
        g_assert_not_reached();
    }
}

static void test_acpi_rxsdt_table(test_data *data)
{
    const char *sig = "RSDT";
    AcpiSdtTable rsdt = {};
    int entry_size = 4;
    int addr_off = 16 /* RsdtAddress */;
    uint8_t *ent;

    if (data->rsdp_table[15 /* Revision offset */] != 0) {
        addr_off = 24 /* XsdtAddress */;
        entry_size = 8;
        sig = "XSDT";
    }
    /* read [RX]SDT table */
    acpi_fetch_table(data->qts, &rsdt.aml, &rsdt.aml_len,
                     &data->rsdp_table[addr_off], entry_size, sig, true);

    /* Load all tables and add to test list directly RSDT referenced tables */
    ACPI_FOREACH_RSDT_ENTRY(rsdt.aml, rsdt.aml_len, ent, entry_size) {
        AcpiSdtTable ssdt_table = {};

        acpi_fetch_table(data->qts, &ssdt_table.aml, &ssdt_table.aml_len, ent,
                         entry_size, NULL, true);
        /* Add table to ASL test tables list */
        g_array_append_val(data->tables, ssdt_table);
    }
    cleanup_table_descriptor(&rsdt);
}

static void test_acpi_fadt_table(test_data *data)
{
    /* FADT table is 1st */
    AcpiSdtTable table = g_array_index(data->tables, typeof(table), 0);
    uint8_t *fadt_aml = table.aml;
    uint32_t fadt_len = table.aml_len;
    uint32_t val;
    int dsdt_offset = 40 /* DSDT */;
    int dsdt_entry_size = 4;

    g_assert(compare_signature(&table, "FACP"));

    /* Since DSDT/FACS isn't in RSDT, add them to ASL test list manually */
    memcpy(&val, fadt_aml + 112 /* Flags */, 4);
    val = le32_to_cpu(val);
    if (!(val & 1UL << 20 /* HW_REDUCED_ACPI */)) {
        acpi_fetch_table(data->qts, &table.aml, &table.aml_len,
                         fadt_aml + 36 /* FIRMWARE_CTRL */, 4, "FACS", false);
        g_array_append_val(data->tables, table);
    }

    memcpy(&val, fadt_aml + dsdt_offset, 4);
    val = le32_to_cpu(val);
    if (!val) {
        dsdt_offset = 140 /* X_DSDT */;
        dsdt_entry_size = 8;
    }
    acpi_fetch_table(data->qts, &table.aml, &table.aml_len,
                     fadt_aml + dsdt_offset, dsdt_entry_size, "DSDT", true);
    g_array_append_val(data->tables, table);

    memset(fadt_aml + 36, 0, 4); /* sanitize FIRMWARE_CTRL ptr */
    memset(fadt_aml + 40, 0, 4); /* sanitize DSDT ptr */
    if (fadt_aml[8 /* FADT Major Version */] >= 3) {
        memset(fadt_aml + 132, 0, 8); /* sanitize X_FIRMWARE_CTRL ptr */
        memset(fadt_aml + 140, 0, 8); /* sanitize X_DSDT ptr */
    }

    /* update checksum */
    fadt_aml[9 /* Checksum */] = 0;
    fadt_aml[9 /* Checksum */] -= acpi_calc_checksum(fadt_aml, fadt_len);
}

static void dump_aml_files(test_data *data, bool rebuild)
{
    AcpiSdtTable *sdt, *exp_sdt;
    GError *error = NULL;
    gchar *aml_file = NULL;
    test_data exp_data = {};
    gint fd;
    ssize_t ret;
    int i;

    exp_data.tables = load_expected_aml(data);
    for (i = 0; i < data->tables->len; ++i) {
        const char *ext = data->variant ? data->variant : "";
        sdt = &g_array_index(data->tables, AcpiSdtTable, i);
        exp_sdt = &g_array_index(exp_data.tables, AcpiSdtTable, i);
        g_assert(sdt->aml);
        g_assert(exp_sdt->aml);

        if (rebuild) {
            aml_file = g_strdup_printf("%s/%s/%s/%.4s%s", data_dir,
                                       data->arch, data->machine,
                                       sdt->aml, ext);

            if (!g_file_test(aml_file, G_FILE_TEST_EXISTS) &&
                sdt->aml_len == exp_sdt->aml_len &&
                !memcmp(sdt->aml, exp_sdt->aml, sdt->aml_len)) {
                /* identical tables, no need to write new files */
                g_free(aml_file);
                continue;
            }
            fd = g_open(aml_file, O_WRONLY|O_TRUNC|O_CREAT,
                        S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
            if (fd < 0) {
                perror(aml_file);
            }
            g_assert(fd >= 0);
        } else {
            fd = g_file_open_tmp("aml-XXXXXX", &sdt->aml_file, &error);
            g_assert_no_error(error);
        }

        ret = qemu_write_full(fd, sdt->aml, sdt->aml_len);
        g_assert(ret == sdt->aml_len);

        close(fd);

        g_free(aml_file);
    }
    free_test_data(&exp_data);
}

static bool create_tmp_asl(AcpiSdtTable *sdt)
{
    GError *error = NULL;
    gint fd;

    fd = g_file_open_tmp("asl-XXXXXX.dsl", &sdt->asl_file, &error);
    g_assert_no_error(error);
    close(fd);

    return false;
}

static bool load_asl(GArray *sdts, AcpiSdtTable *sdt)
{
    AcpiSdtTable *temp;
    GError *error = NULL;
    GString *command_line = g_string_new(iasl);
    gchar *out, *out_err;
    gboolean ret;
    int i;

    create_tmp_asl(sdt);

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
        ret = g_file_get_contents(sdt->asl_file, &sdt->asl,
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
#define BLOCK_NAME_END ","

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
    GError *error = NULL;
    gboolean ret;
    gsize aml_len;

    GArray *exp_tables = g_array_new(false, true, sizeof(AcpiSdtTable));
    if (verbosity_level >= 2) {
        fputc('\n', stderr);
    }
    for (i = 0; i < data->tables->len; ++i) {
        AcpiSdtTable exp_sdt;
        gchar *aml_file = NULL;
        const char *ext = data->variant ? data->variant : "";

        sdt = &g_array_index(data->tables, AcpiSdtTable, i);

        memset(&exp_sdt, 0, sizeof(exp_sdt));

try_again:
        aml_file = g_strdup_printf("%s/%s/%s/%.4s%s", data_dir, data->arch,
                                   data->machine, sdt->aml, ext);
        if (verbosity_level >= 2) {
            fprintf(stderr, "Looking for expected file '%s'\n", aml_file);
        }
        if (g_file_test(aml_file, G_FILE_TEST_EXISTS)) {
            exp_sdt.aml_file = aml_file;
        } else if (*ext != '\0') {
            /* try fallback to generic (extension less) expected file */
            ext = "";
            g_free(aml_file);
            goto try_again;
        }
        g_assert(exp_sdt.aml_file);
        if (verbosity_level >= 2) {
            fprintf(stderr, "Using expected file '%s'\n", aml_file);
        }
        ret = g_file_get_contents(aml_file, (gchar **)&exp_sdt.aml,
                                  &aml_len, &error);
        exp_sdt.aml_len = aml_len;
        g_assert(ret);
        g_assert_no_error(error);
        g_assert(exp_sdt.aml);
        if (!exp_sdt.aml_len) {
            fprintf(stderr, "Warning! zero length expected file '%s'\n",
                    aml_file);
        }

        g_array_append_val(exp_tables, exp_sdt);
    }

    return exp_tables;
}

static bool test_acpi_find_diff_allowed(AcpiSdtTable *sdt)
{
    const gchar *allowed_diff_file[] = {
#include "bios-tables-test-allowed-diff.h"
        NULL
    };
    const gchar **f;

    for (f = allowed_diff_file; *f; ++f) {
        if (!g_strcmp0(sdt->aml_file, *f)) {
            return true;
        }
    }
    return false;
}

/* test the list of tables in @data->tables against reference tables */
static void test_acpi_asl(test_data *data)
{
    int i;
    AcpiSdtTable *sdt, *exp_sdt;
    test_data exp_data = {};
    gboolean exp_err, err, all_tables_match = true;

    exp_data.tables = load_expected_aml(data);
    dump_aml_files(data, false);
    for (i = 0; i < data->tables->len; ++i) {
        GString *asl, *exp_asl;

        sdt = &g_array_index(data->tables, AcpiSdtTable, i);
        exp_sdt = &g_array_index(exp_data.tables, AcpiSdtTable, i);

        if (sdt->aml_len == exp_sdt->aml_len &&
            !memcmp(sdt->aml, exp_sdt->aml, sdt->aml_len)) {
            /* Identical table binaries: no need to disassemble. */
            continue;
        }

        fprintf(stderr,
                "acpi-test: Warning! %.4s binary file mismatch. "
                "Actual [aml:%s], Expected [aml:%s].\n"
                "See source file tests/qtest/bios-tables-test.c "
                "for instructions on how to update expected files.\n",
                exp_sdt->aml, sdt->aml_file, exp_sdt->aml_file);

        all_tables_match = all_tables_match &&
            test_acpi_find_diff_allowed(exp_sdt);

        /*
         *  don't try to decompile if IASL isn't present, in this case user
         * will just 'get binary file mismatch' warnings and test failure
         */
        if (!iasl) {
            continue;
        }

        err = load_asl(data->tables, sdt);
        asl = normalize_asl(sdt->asl);

        /*
         * If expected file is empty - it's likely that it was a stub just
         * created for step 1 above: we do want to decompile the actual one.
         */
        if (exp_sdt->aml_len) {
            exp_err = load_asl(exp_data.tables, exp_sdt);
            exp_asl = normalize_asl(exp_sdt->asl);
        } else {
            exp_err = create_tmp_asl(exp_sdt);
            exp_asl = g_string_new("");
        }

        /* TODO: check for warnings */
        g_assert(!err || exp_err || !exp_sdt->aml_len);

        if (g_strcmp0(asl->str, exp_asl->str)) {
            sdt->tmp_files_retain = true;
            if (exp_err) {
                fprintf(stderr,
                        "Warning! iasl couldn't parse the expected aml\n");
            } else {
                exp_sdt->tmp_files_retain = true;
                fprintf(stderr,
                        "acpi-test: Warning! %.4s mismatch. "
                        "Actual [asl:%s, aml:%s], Expected [asl:%s, aml:%s].\n",
                        exp_sdt->aml, sdt->asl_file, sdt->aml_file,
                        exp_sdt->asl_file, exp_sdt->aml_file);
                fflush(stderr);
                if (verbosity_level >= 1) {
                    const char *diff_env = getenv("DIFF");
                    const char *diff_cmd = diff_env ? diff_env : "diff -U 16";
                    char *diff = g_strdup_printf("%s %s %s", diff_cmd,
                                                 exp_sdt->asl_file, sdt->asl_file);
                    int out = dup(STDOUT_FILENO);
                    int ret G_GNUC_UNUSED;
                    int dupret;

                    g_assert(out >= 0);
                    dupret = dup2(STDERR_FILENO, STDOUT_FILENO);
                    g_assert(dupret >= 0);
                    ret = system(diff) ;
                    dupret = dup2(out, STDOUT_FILENO);
                    g_assert(dupret >= 0);
                    close(out);
                    g_free(diff);
                }
            }
        }
        g_string_free(asl, true);
        g_string_free(exp_asl, true);
    }
    if (!iasl && !all_tables_match) {
        fprintf(stderr, "to see ASL diff between mismatched files install IASL,"
                " rebuild QEMU from scratch and re-run tests with V=1"
                " environment variable set");
    }
    g_assert(all_tables_match);

    free_test_data(&exp_data);
}

static bool smbios_ep2_table_ok(test_data *data, uint32_t addr)
{
    struct smbios_21_entry_point *ep_table = &data->smbios_ep_table.ep21;

    qtest_memread(data->qts, addr, ep_table, sizeof(*ep_table));
    if (memcmp(ep_table->anchor_string, "_SM_", 4)) {
        return false;
    }
    if (memcmp(ep_table->intermediate_anchor_string, "_DMI_", 5)) {
        return false;
    }
    if (ep_table->structure_table_length == 0) {
        return false;
    }
    if (ep_table->number_of_structures == 0) {
        return false;
    }
    if (acpi_calc_checksum((uint8_t *)ep_table, sizeof *ep_table) ||
        acpi_calc_checksum((uint8_t *)ep_table + 0x10,
                           sizeof *ep_table - 0x10)) {
        return false;
    }
    return true;
}

static bool smbios_ep3_table_ok(test_data *data, uint64_t addr)
{
    struct smbios_30_entry_point *ep_table = &data->smbios_ep_table.ep30;

    qtest_memread(data->qts, addr, ep_table, sizeof(*ep_table));
    if (memcmp(ep_table->anchor_string, "_SM3_", 5)) {
        return false;
    }

    if (acpi_calc_checksum((uint8_t *)ep_table, sizeof *ep_table)) {
        return false;
    }

    return true;
}

static SmbiosEntryPointType test_smbios_entry_point(test_data *data)
{
    uint32_t off;

    /* find smbios entry point structure */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "_SM_", sig3[] = "_SM3_";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = qtest_readb(data->qts, off + i);
        }

        if (!memcmp(sig, "_SM_", sizeof sig)) {
            /* signature match, but is this a valid entry point? */
            if (smbios_ep2_table_ok(data, off)) {
                data->smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE_32] = off;
            }
        }

        for (i = 0; i < sizeof sig3 - 1; ++i) {
            sig3[i] = qtest_readb(data->qts, off + i);
        }

        if (!memcmp(sig3, "_SM3_", sizeof sig3)) {
            if (smbios_ep3_table_ok(data, off)) {
                data->smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE_64] = off;
                /* found 64-bit entry point, no need to look for 32-bit one */
                break;
            }
        }
    }

    /* found at least one entry point */
    g_assert_true(data->smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE_32] ||
                  data->smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE_64]);

    return data->smbios_ep_addr[SMBIOS_ENTRY_POINT_TYPE_64] ?
           SMBIOS_ENTRY_POINT_TYPE_64 : SMBIOS_ENTRY_POINT_TYPE_32;
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

static void smbios_cpu_test(test_data *data, uint32_t addr,
                            SmbiosEntryPointType ep_type)
{
    uint8_t core_count, expected_core_count = data->smbios_core_count;
    uint8_t thread_count, expected_thread_count = data->smbios_thread_count;
    uint16_t speed, expected_speed[2];
    uint16_t core_count2, expected_core_count2 = data->smbios_core_count2;
    uint16_t thread_count2, expected_thread_count2 = data->smbios_thread_count2;
    int offset[2];
    int i;

    /* Check CPU speed for backward compatibility */
    offset[0] = offsetof(struct smbios_type_4, max_speed);
    offset[1] = offsetof(struct smbios_type_4, current_speed);
    expected_speed[0] = data->smbios_cpu_max_speed ? : 2000;
    expected_speed[1] = data->smbios_cpu_curr_speed ? : 2000;

    for (i = 0; i < 2; i++) {
        speed = qtest_readw(data->qts, addr + offset[i]);
        g_assert_cmpuint(speed, ==, expected_speed[i]);
    }

    core_count = qtest_readb(data->qts,
                    addr + offsetof(struct smbios_type_4, core_count));

    if (expected_core_count) {
        g_assert_cmpuint(core_count, ==, expected_core_count);
    }

    thread_count = qtest_readb(data->qts,
                       addr + offsetof(struct smbios_type_4, thread_count));

    if (expected_thread_count) {
        g_assert_cmpuint(thread_count, ==, expected_thread_count);
    }

    if (ep_type == SMBIOS_ENTRY_POINT_TYPE_64) {
        core_count2 = qtest_readw(data->qts,
                          addr + offsetof(struct smbios_type_4, core_count2));

        /* Core Count has reached its limit, checking Core Count 2 */
        if (expected_core_count == 0xFF && expected_core_count2) {
            g_assert_cmpuint(core_count2, ==, expected_core_count2);
        }

        thread_count2 = qtest_readw(data->qts,
                            addr + offsetof(struct smbios_type_4,
                            thread_count2));

        /* Thread Count has reached its limit, checking Thread Count 2 */
        if (expected_thread_count == 0xFF && expected_thread_count2) {
            g_assert_cmpuint(thread_count2, ==, expected_thread_count2);
        }
    }
}

static void smbios_type4_count_test(test_data *data, int type4_count)
{
    int expected_type4_count = data->type4_count;

    if (expected_type4_count) {
        g_assert_cmpuint(type4_count, ==, expected_type4_count);
    }
}

static void test_smbios_structs(test_data *data, SmbiosEntryPointType ep_type)
{
    DECLARE_BITMAP(struct_bitmap, SMBIOS_MAX_TYPE+1) = { 0 };

    SmbiosEntryPoint *ep_table = &data->smbios_ep_table;
    int i = 0, len, max_len = 0, type4_count = 0;
    uint8_t type, prv, crt;
    uint64_t addr;

    if (ep_type == SMBIOS_ENTRY_POINT_TYPE_32) {
        addr = le32_to_cpu(ep_table->ep21.structure_table_address);
    } else {
        addr = le64_to_cpu(ep_table->ep30.structure_table_address);
    }

    /* walk the smbios tables */
    do {

        /* grab type and formatted area length from struct header */
        type = qtest_readb(data->qts, addr);
        g_assert_cmpuint(type, <=, SMBIOS_MAX_TYPE);
        len = qtest_readb(data->qts, addr + 1);

        /* single-instance structs must not have been encountered before */
        if (smbios_single_instance(type)) {
            g_assert(!test_bit(type, struct_bitmap));
        }
        set_bit(type, struct_bitmap);

        if (type == 4) {
            smbios_cpu_test(data, addr, ep_type);
            type4_count++;
        }

        /* seek to end of unformatted string area of this struct ("\0\0") */
        prv = crt = 1;
        while (prv || crt) {
            prv = crt;
            crt = qtest_readb(data->qts, addr + len);
            len++;
        }

        /* keep track of max. struct size */
        if (ep_type == SMBIOS_ENTRY_POINT_TYPE_32 && max_len < len) {
            max_len = len;
            g_assert_cmpuint(max_len, <=, ep_table->ep21.max_structure_size);
        }

        /* start of next structure */
        addr += len;

    /*
     * Until all structures have been scanned (ep21)
     * or an EOF structure is found (ep30)
     */
    } while (ep_type == SMBIOS_ENTRY_POINT_TYPE_32 ?
                ++i < le16_to_cpu(ep_table->ep21.number_of_structures) :
                type != 127);

    if (ep_type == SMBIOS_ENTRY_POINT_TYPE_32) {
        /*
         * Total table length and max struct size
         * must match entry point values
         */
        g_assert_cmpuint(le16_to_cpu(ep_table->ep21.structure_table_length), ==,
            addr - le32_to_cpu(ep_table->ep21.structure_table_address));

        g_assert_cmpuint(le16_to_cpu(ep_table->ep21.max_structure_size), ==,
            max_len);
    }

    /* required struct types must all be present */
    for (i = 0; i < data->required_struct_types_len; i++) {
        g_assert(test_bit(data->required_struct_types[i], struct_bitmap));
    }

    smbios_type4_count_test(data, type4_count);
}

static void test_acpi_load_tables(test_data *data)
{
    if (data->uefi_fl1 && data->uefi_fl2) { /* use UEFI */
        g_assert(data->scan_len);
        data->rsdp_addr = acpi_find_rsdp_address_uefi(data->qts,
            data->ram_start, data->scan_len);
    } else {
        boot_sector_test(data->qts);
        data->rsdp_addr = acpi_find_rsdp_address(data->qts);
        g_assert_cmphex(data->rsdp_addr, <, 0x100000);
    }

    data->tables = g_array_new(false, true, sizeof(AcpiSdtTable));
    test_acpi_rsdp_table(data);
    test_acpi_rxsdt_table(data);
    test_acpi_fadt_table(data);
}

static char *test_acpi_create_args(test_data *data, const char *params)
{
    char *args;

    if (data->uefi_fl1 && data->uefi_fl2) { /* use UEFI */
        /*
         * TODO: convert '-drive if=pflash' to new syntax (see e33763be7cd3)
         * when arm/virt boad starts to support it.
         */
        if (data->cd) {
            args = g_strdup_printf("-machine %s%s %s -accel tcg "
                "-nodefaults -nographic "
                "-drive if=pflash,format=raw,file=%s,readonly=on "
                "-drive if=pflash,format=raw,file=%s,snapshot=on -cdrom %s %s",
                data->machine, data->machine_param ?: "",
                data->tcg_only ? "" : "-accel kvm",
                data->uefi_fl1, data->uefi_fl2, data->cd, params ? params : "");
        } else {
            args = g_strdup_printf("-machine %s%s %s -accel tcg "
                "-nodefaults -nographic "
                "-drive if=pflash,format=raw,file=%s,readonly=on "
                "-drive if=pflash,format=raw,file=%s,snapshot=on %s",
                data->machine, data->machine_param ?: "",
                data->tcg_only ? "" : "-accel kvm",
                data->uefi_fl1, data->uefi_fl2, params ? params : "");
        }
    } else {
        args = g_strdup_printf("-machine %s%s %s -accel tcg "
            "-net none %s "
            "-drive id=hd0,if=none,file=%s,format=raw "
            "-device %s,drive=hd0 ",
             data->machine, data->machine_param ?: "",
             data->tcg_only ? "" : "-accel kvm",
             params ? params : "", disk,
             data->blkdev ?: "ide-hd");
    }
    return args;
}

static void test_vm_prepare(const char *params, test_data *data)
{
    char *args = test_acpi_create_args(data, params);
    data->qts = qtest_init(args);
    g_free(args);
}

static void process_smbios_tables_noexit(test_data *data)
{
    /*
     * TODO: make SMBIOS tests work with UEFI firmware,
     * Bug on uefi-test-tools to provide entry point:
     * https://bugs.launchpad.net/qemu/+bug/1821884
     */
    if (!(data->uefi_fl1 && data->uefi_fl2)) {
        SmbiosEntryPointType ep_type = test_smbios_entry_point(data);
        test_smbios_structs(data, ep_type);
    }
}

static void test_smbios(const char *params, test_data *data)
{
    test_vm_prepare(params, data);
    boot_sector_test(data->qts);
    process_smbios_tables_noexit(data);
    qtest_quit(data->qts);
}

static void process_acpi_tables_noexit(test_data *data)
{
    test_acpi_load_tables(data);

    if (getenv(ACPI_REBUILD_EXPECTED_AML)) {
        dump_aml_files(data, true);
    } else {
        test_acpi_asl(data);
    }

    process_smbios_tables_noexit(data);
}

static void process_acpi_tables(test_data *data)
{
    process_acpi_tables_noexit(data);
    qtest_quit(data->qts);
}

static void test_acpi_one(const char *params, test_data *data)
{
    test_vm_prepare(params, data);
    process_acpi_tables(data);
}

static uint8_t base_required_struct_types[] = {
    0, 1, 3, 4, 16, 17, 19, 32, 127
};

static void test_acpi_piix4_tcg(void)
{
    test_data data = {};

    /* Supplying -machine accel argument overrides the default (qtest).
     * This is to make guest actually run.
     */
    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one(NULL, &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_bridge(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".bridge";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_vm_prepare("-S"
        " -device pci-bridge,chassis_nr=1"
        " -device pci-bridge,bus=pci.1,addr=1.0,chassis_nr=2"
        " -device pci-testdev,bus=pci.0,addr=5.0"
        " -device pci-testdev,bus=pci.1", &data);

    /* hotplugged bridges section */
    qtest_qmp_device_add(data.qts, "pci-bridge", "hpbr",
        "{'bus': 'pci.1', 'addr': '2.0', 'chassis_nr': 3 }");
    qtest_qmp_device_add(data.qts, "pci-bridge", "hpbr_multifunc",
        "{'bus': 'pci.1', 'addr': '0xf.1', 'chassis_nr': 4 }");
    qtest_qmp_device_add(data.qts, "pci-bridge", "hpbrhost",
        "{'bus': 'pci.0', 'addr': '4.0', 'chassis_nr': 5 }");
    qtest_qmp_device_add(data.qts, "pci-testdev", "d1", "{'bus': 'pci.0' }");
    qtest_qmp_device_add(data.qts, "pci-testdev", "d2", "{'bus': 'pci.1' }");
    qtest_qmp_device_add(data.qts, "pci-testdev", "d3", "{'bus': 'hpbr', "
                                   "'addr': '1.0' }");
    qtest_qmp_send(data.qts, "{'execute':'cont' }");
    qtest_qmp_eventwait(data.qts, "RESUME");

    process_acpi_tables_noexit(&data);
    free_test_data(&data);

    /* check that reboot/reset doesn't change any ACPI tables  */
    qtest_system_reset(data.qts);
    process_acpi_tables(&data);
    free_test_data(&data);
}

static void test_acpi_piix4_no_root_hotplug(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".roothp";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one("-global PIIX4_PM.acpi-root-pci-hotplug=off "
                  "-device pci-bridge,chassis_nr=1 "
                  "-device pci-bridge,bus=pci.1,addr=1.0,chassis_nr=2 "
                  "-device pci-testdev,bus=pci.0 "
                  "-device pci-testdev,bus=pci.1", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_no_bridge_hotplug(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".hpbridge";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one("-global PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off "
                  "-device pci-bridge,chassis_nr=1 "
                  "-device pci-bridge,bus=pci.1,addr=1.0,chassis_nr=2 "
                  "-device pci-testdev,bus=pci.0 "
                  "-device pci-testdev,bus=pci.1,addr=2.0", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_no_acpi_pci_hotplug(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".hpbrroot";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one("-global PIIX4_PM.acpi-root-pci-hotplug=off "
                  "-global PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off "
                  "-device pci-bridge,chassis_nr=1,addr=4.0 "
                  "-device pci-testdev,bus=pci.0,addr=5.0 "
                  "-device pci-testdev,bus=pci.0,addr=6.0,acpi-index=101 "
                  "-device pci-testdev,bus=pci.1,addr=1.0 "
                  "-device pci-testdev,bus=pci.1,addr=2.0,acpi-index=201 "
                  "-device pci-bridge,id=nhpbr,chassis_nr=2,shpc=off,addr=7.0 "
                  "-device pci-testdev,bus=nhpbr,addr=1.0,acpi-index=301 "
                  , &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch = "x86";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one(NULL, &data);
    free_test_data(&data);

    data.smbios_cpu_max_speed = 3000;
    data.smbios_cpu_curr_speed = 2600;
    test_acpi_one("-smbios type=4,max-speed=3000,current-speed=2600", &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_type4_count(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".type4-count",
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types),
        .type4_count = 5,
    };

    test_acpi_one("-machine smbios-entry-point-type=64 "
                  "-smp cpus=100,maxcpus=120,sockets=5,"
                  "dies=2,cores=4,threads=3", &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_core_count(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".core-count",
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types),
        .smbios_core_count = 9,
        .smbios_core_count2 = 9,
    };

    test_acpi_one("-machine smbios-entry-point-type=64 "
                  "-smp 54,sockets=2,dies=3,cores=3,threads=3",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_core_count2(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".core-count2",
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types),
        .smbios_core_count = 0xFF,
        .smbios_core_count2 = 260,
    };

    test_acpi_one("-machine smbios-entry-point-type=64 "
                  "-smp 260,dies=2,cores=130,threads=1",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_thread_count(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".thread-count",
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types),
        .smbios_thread_count = 27,
        .smbios_thread_count2 = 27,
    };

    test_acpi_one("-machine smbios-entry-point-type=64 "
                  "-smp cpus=15,maxcpus=54,sockets=2,dies=3,cores=3,threads=3",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_thread_count2(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".thread-count2",
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types),
        .smbios_thread_count = 0xFF,
        .smbios_thread_count2 = 260,
    };

    test_acpi_one("-machine smbios-entry-point-type=64 "
                  "-smp cpus=210,maxcpus=260,dies=2,cores=65,threads=2",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_bridge(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".bridge";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one("-device pci-bridge,chassis_nr=1,id=br1"
                  " -device pci-testdev,bus=pcie.0"
                  " -device pci-testdev,bus=br1", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_no_acpi_hotplug(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".noacpihp";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);
    test_acpi_one("-global ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off"
        " -device pci-testdev,bus=pcie.0,acpi-index=101,addr=3.0"
        " -device pci-bridge,chassis_nr=1,id=shpcbr,addr=4.0"
        " -device pci-testdev,bus=shpcbr,addr=1.0,acpi-index=201"
        " -device pci-bridge,chassis_nr=2,shpc=off,id=noshpcbr,addr=5.0"
        " -device pci-testdev,bus=noshpcbr,addr=1.0,acpi-index=301"
        " -device pcie-root-port,id=hprp,port=0x0,chassis=1,addr=6.0"
        " -device pci-testdev,bus=hprp,acpi-index=401"
        " -device pcie-root-port,id=nohprp,port=0x0,chassis=2,hotplug=off,"
                                 "addr=7.0"
        " -device pci-testdev,bus=nohprp,acpi-index=501"
        " -device pcie-root-port,id=nohprpint,port=0x0,chassis=3,hotplug=off,"
                                 "multifunction=on,addr=8.0"
        " -device pci-testdev,bus=nohprpint,acpi-index=601,addr=0.1"
        " -device pcie-root-port,id=hprp2,port=0x0,chassis=4,bus=nohprpint,"
                                 "addr=0.2"
        " -device pci-testdev,bus=hprp2,acpi-index=602"
        , &data);
    free_test_data(&data);
}

static void test_acpi_q35_multif_bridge(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".multi-bridge",
    };
    test_vm_prepare("-S"
        " -device virtio-balloon,id=balloon0,addr=0x4.0x2"
        " -device pcie-root-port,id=rp0,multifunction=on,"
                  "port=0x0,chassis=1,addr=0x2"
        " -device pcie-root-port,id=rp1,port=0x1,chassis=2,addr=0x3.0x1"
        " -device pcie-root-port,id=rp2,port=0x0,chassis=3,bus=rp1,addr=0.0"
        " -device pci-bridge,bus=rp2,chassis_nr=4,id=br1"
        " -device pcie-root-port,id=rphptgt1,port=0x0,chassis=5,addr=2.1"
        " -device pcie-root-port,id=rphptgt2,port=0x0,chassis=6,addr=2.2"
        " -device pcie-root-port,id=rphptgt3,port=0x0,chassis=7,addr=2.3"
        " -device pci-testdev,bus=pcie.0,addr=2.4"
        " -device pci-testdev,bus=pcie.0,addr=2.5,acpi-index=102"
        " -device pci-testdev,bus=pcie.0,addr=5.0"
        " -device pci-testdev,bus=pcie.0,addr=0xf.0,acpi-index=101"
        " -device pci-testdev,bus=rp0,addr=0.0"
        " -device pci-testdev,bus=br1"
        " -device pcie-root-port,id=rpnohp,chassis=8,addr=0xA.0,hotplug=off"
        " -device pcie-root-port,id=rp3,chassis=9,bus=rpnohp"
        , &data);

    /* hotplugged bridges section */
    qtest_qmp_device_add(data.qts, "pci-bridge", "hpbr1",
        "{'bus': 'br1', 'addr': '6.0', 'chassis_nr': 128 }");
    qtest_qmp_device_add(data.qts, "pci-bridge", "hpbr2-multiif",
        "{ 'bus': 'br1', 'addr': '2.2', 'chassis_nr': 129 }");
    qtest_qmp_device_add(data.qts, "pcie-pci-bridge", "hpbr3",
        "{'bus': 'rphptgt1', 'addr': '0.0' }");
    qtest_qmp_device_add(data.qts, "pcie-root-port", "hprp",
        "{'bus': 'rphptgt2', 'addr': '0.0' }");
    qtest_qmp_device_add(data.qts, "pci-testdev", "hpnic",
        "{'bus': 'rphptgt3', 'addr': '0.0' }");
    qtest_qmp_send(data.qts, "{'execute':'cont' }");
    qtest_qmp_eventwait(data.qts, "RESUME");

    process_acpi_tables_noexit(&data);
    free_test_data(&data);

    /* check that reboot/reset doesn't change any ACPI tables  */
    qtest_system_reset(data.qts);
    process_acpi_tables(&data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_mmio64(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".mmio64",
        .tcg_only = true,
        .required_struct_types = base_required_struct_types,
        .required_struct_types_len = ARRAY_SIZE(base_required_struct_types)
    };

    test_acpi_one("-m 128M,slots=1,maxmem=2G "
                  "-cpu Opteron_G1 "
                  "-object memory-backend-ram,id=ram0,size=128M "
                  "-numa node,memdev=ram0 "
                  "-device pci-testdev,membar=2G",
                  &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_cphp(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".cphp";
    test_acpi_one("-smp 2,cores=3,sockets=2,maxcpus=6"
                  " -object memory-backend-ram,id=ram0,size=64M"
                  " -object memory-backend-ram,id=ram1,size=64M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_cphp(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".cphp";
    test_acpi_one(" -smp 2,cores=3,sockets=2,maxcpus=6"
                  " -object memory-backend-ram,id=ram0,size=64M"
                  " -object memory-backend-ram,id=ram1,size=64M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21",
                  &data);
    free_test_data(&data);
}

static uint8_t ipmi_required_struct_types[] = {
    0, 1, 3, 4, 16, 17, 19, 32, 38, 127
};

static void test_acpi_q35_tcg_ipmi(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".ipmibt";
    data.required_struct_types = ipmi_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(ipmi_required_struct_types);
    test_acpi_one("-device ipmi-bmc-sim,id=bmc0"
                  " -device isa-ipmi-bt,bmc=bmc0",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_smbus_ipmi(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".ipmismbus";
    data.required_struct_types = ipmi_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(ipmi_required_struct_types);
    test_acpi_one("-device ipmi-bmc-sim,id=bmc0"
                  " -device smbus-ipmi,bmc=bmc0",
                  &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_ipmi(void)
{
    test_data data = {};

    /* Supplying -machine accel argument overrides the default (qtest).
     * This is to make guest actually run.
     */
    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".ipmikcs";
    data.required_struct_types = ipmi_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(ipmi_required_struct_types);
    test_acpi_one("-device ipmi-bmc-sim,id=bmc0"
                  " -device isa-ipmi-kcs,irq=0,bmc=bmc0",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_memhp(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".memhp";
    test_acpi_one(" -m 128,slots=3,maxmem=1G"
                  " -object memory-backend-ram,id=ram0,size=64M"
                  " -object memory-backend-ram,id=ram1,size=64M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21",
                  &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_memhp(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".memhp";
    test_acpi_one(" -m 128,slots=3,maxmem=1G"
                  " -object memory-backend-ram,id=ram0,size=64M"
                  " -object memory-backend-ram,id=ram1,size=64M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21",
                  &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_nosmm(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".nosmm";
    test_acpi_one("-machine smm=off", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_smm_compat(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".smm-compat";
    test_acpi_one("-global PIIX4_PM.smm-compat=on", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_smm_compat_nosmm(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".smm-compat-nosmm";
    test_acpi_one("-global PIIX4_PM.smm-compat=on -machine smm=off", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_nohpet(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.machine_param = ",hpet=off";
    data.variant = ".nohpet";
    test_acpi_one(NULL, &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_numamem(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".numamem";
    test_acpi_one(" -object memory-backend-ram,id=ram0,size=128M"
                  " -numa node -numa node,memdev=ram0", &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_xapic(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".xapic";
    test_acpi_one(" -object memory-backend-ram,id=ram0,size=128M"
                  " -numa node -numa node,memdev=ram0"
                  " -machine kernel-irqchip=on -smp 1,maxcpus=288", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_nosmm(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".nosmm";
    test_acpi_one("-machine smm=off", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_smm_compat(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".smm-compat";
    test_acpi_one("-global ICH9-LPC.smm-compat=on", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_smm_compat_nosmm(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".smm-compat-nosmm";
    test_acpi_one("-global ICH9-LPC.smm-compat=on -machine smm=off", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_nohpet(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.machine_param = ",hpet=off";
    data.variant = ".nohpet";
    test_acpi_one(NULL, &data);
    free_test_data(&data);
}

static void test_acpi_q35_kvm_dmar(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".dmar";
    test_acpi_one("-machine kernel-irqchip=split -accel kvm"
                  " -device intel-iommu,intremap=on,device-iotlb=on", &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_ivrs(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86",
    data.variant = ".ivrs";
    data.tcg_only = true,
    test_acpi_one(" -device amd-iommu", &data);
    free_test_data(&data);
}

static void test_acpi_piix4_tcg_numamem(void)
{
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.variant = ".numamem";
    test_acpi_one(" -object memory-backend-ram,id=ram0,size=128M"
                  " -numa node -numa node,memdev=ram0", &data);
    free_test_data(&data);
}

uint64_t tpm_tis_base_addr;

static void test_acpi_tcg_tpm(const char *machine, const char *arch,
                              const char *tpm_if, uint64_t base,
                              enum TPMVersion tpm_version)
{
    gchar *tmp_dir_name = g_strdup_printf("qemu-test_acpi_%s_tcg_%s.XXXXXX",
                                          machine, tpm_if);
    char *tmp_path = g_dir_make_tmp(tmp_dir_name, NULL);
    TPMTestState test;
    test_data data = {};
    GThread *thread;
    const char *suffix = tpm_version == TPM_VERSION_2_0 ? "tpm2" : "tpm12";
    char *args, *variant = g_strdup_printf(".%s.%s", tpm_if, suffix);

    tpm_tis_base_addr = base;

    module_call_init(MODULE_INIT_QOM);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = tpm_version;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    data.machine = machine;
    data.arch = arch;
    data.variant = variant;

    args = g_strdup_printf(
        " -chardev socket,id=chr,path=%s"
        " -tpmdev emulator,id=dev,chardev=chr"
        " -device tpm-%s,tpmdev=dev",
        test.addr->u.q_unix.path, tpm_if);

    test_acpi_one(args, &data);

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(variant);
    g_free(tmp_path);
    g_free(tmp_dir_name);
    g_free(args);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_tpm2_tis(void)
{
    test_acpi_tcg_tpm("q35", "x86", "tis", 0xFED40000, TPM_VERSION_2_0);
}

static void test_acpi_q35_tcg_tpm12_tis(void)
{
    test_acpi_tcg_tpm("q35", "x86", "tis", 0xFED40000, TPM_VERSION_1_2);
}

static void test_acpi_tcg_dimm_pxm(const char *machine, const char *arch)
{
    test_data data = {};

    data.machine = machine;
    data.arch    = arch;
    data.variant = ".dimmpxm";
    test_acpi_one(" -machine nvdimm=on,nvdimm-persistence=cpu"
                  " -smp 4,sockets=4"
                  " -m 128M,slots=3,maxmem=1G"
                  " -object memory-backend-ram,id=ram0,size=32M"
                  " -object memory-backend-ram,id=ram1,size=32M"
                  " -object memory-backend-ram,id=ram2,size=32M"
                  " -object memory-backend-ram,id=ram3,size=32M"
                  " -numa node,memdev=ram0,nodeid=0"
                  " -numa node,memdev=ram1,nodeid=1"
                  " -numa node,memdev=ram2,nodeid=2"
                  " -numa node,memdev=ram3,nodeid=3"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=1,socket-id=1"
                  " -numa cpu,node-id=2,socket-id=2"
                  " -numa cpu,node-id=3,socket-id=3"
                  " -object memory-backend-ram,id=ram4,size=128M"
                  " -object memory-backend-ram,id=nvm0,size=128M"
                  " -device pc-dimm,id=dimm0,memdev=ram4,node=1"
                  " -device nvdimm,id=dimm1,memdev=nvm0,node=2",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_dimm_pxm(void)
{
    test_acpi_tcg_dimm_pxm(MACHINE_Q35, "x86");
}

static void test_acpi_piix4_tcg_dimm_pxm(void)
{
    test_acpi_tcg_dimm_pxm(MACHINE_PC, "x86");
}

static void test_acpi_aarch64_virt_tcg_memhp(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 256ULL * MiB,
    };

    data.variant = ".memhp";
    test_acpi_one(" -machine nvdimm=on"
                  " -cpu cortex-a57"
                  " -m 256M,slots=3,maxmem=1G"
                  " -object memory-backend-ram,id=ram0,size=128M"
                  " -object memory-backend-ram,id=ram1,size=128M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21"
                  " -object memory-backend-ram,id=ram2,size=128M"
                  " -object memory-backend-ram,id=nvm0,size=128M"
                  " -device pc-dimm,id=dimm0,memdev=ram2,node=0"
                  " -device nvdimm,id=dimm1,memdev=nvm0,node=1",
                  &data);

    free_test_data(&data);

}

static void test_acpi_aarch64_virt_acpi_pci_hotplug(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 256ULL * MiB,
        .variant = ".acpipcihp",
    };

   /* Use ACPI PCI Hotplug */
   test_acpi_one(" -global acpi-ged.acpi-pci-hotplug-with-bridge-support=on"
                 " -cpu cortex-a57"
                 " -device pcie-root-port,id=pcie.1,bus=pcie.0,chassis=0,slot=1,addr=7.0"
                 " -device pci-testdev,bus=pcie.1",
                 &data);

    free_test_data(&data);
}

static void test_acpi_aarch64_virt_pcie_root_port_hpoff(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 256ULL * MiB,
        .variant = ".hpoffacpiindex",
    };

   /* turn hotplug off on the pcie-root-port and use static acpi-index*/
   test_acpi_one(" -device pcie-root-port,id=pcie.1,chassis=0,"
                                          "slot=1,hotplug=off,addr=7.0"
                 " -device pci-testdev,bus=pcie.1,acpi-index=12"
                 " -cpu cortex-a57",
                 &data);

    free_test_data(&data);
}

static void test_acpi_microvm_prepare(test_data *data)
{
    data->machine = "microvm";
    data->arch = "x86";
    data->required_struct_types = NULL; /* no smbios */
    data->required_struct_types_len = 0;
    data->blkdev = "virtio-blk-device";
}

static void test_acpi_microvm_tcg(void)
{
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    test_acpi_one(" -machine microvm,acpi=on,ioapic2=off,rtc=off",
                  &data);
    free_test_data(&data);
}

static void test_acpi_microvm_usb_tcg(void)
{
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    data.variant = ".usb";
    test_acpi_one(" -machine microvm,acpi=on,ioapic2=off,usb=on,rtc=off",
                  &data);
    free_test_data(&data);
}

static void test_acpi_microvm_rtc_tcg(void)
{
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    data.variant = ".rtc";
    test_acpi_one(" -machine microvm,acpi=on,ioapic2=off,rtc=on",
                  &data);
    free_test_data(&data);
}

static void test_acpi_microvm_pcie_tcg(void)
{
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    data.variant = ".pcie";
    data.tcg_only = true; /* need constant host-phys-bits */
    test_acpi_one(" -machine microvm,acpi=on,ioapic2=off,rtc=off,pcie=on",
                  &data);
    free_test_data(&data);
}

static void test_acpi_microvm_ioapic2_tcg(void)
{
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    data.variant = ".ioapic2";
    test_acpi_one(" -machine microvm,acpi=on,ioapic2=on,rtc=off",
                  &data);
    free_test_data(&data);
}

static void test_acpi_riscv64_virt_tcg_numamem(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "riscv64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-riscv-code.fd",
        .uefi_fl2 = "pc-bios/edk2-riscv-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.riscv64.iso.qcow2",
        .ram_start = 0x80000000ULL,
        .scan_len = 128ULL * MiB,
    };

    data.variant = ".numamem";
    /*
     * RHCT will have ISA string encoded. To reduce the effort
     * of updating expected AML file for any new default ISA extension,
     * use the profile rva22s64.
     */
    test_acpi_one(" -cpu rva22s64"
                  " -object memory-backend-ram,id=ram0,size=128M"
                  " -numa node,memdev=ram0",
                  &data);
    free_test_data(&data);
}

static void test_acpi_aarch64_virt_tcg_numamem(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };

    data.variant = ".numamem";
    test_acpi_one(" -cpu cortex-a57"
                  " -object memory-backend-ram,id=ram0,size=128M"
                  " -numa node,memdev=ram0",
                  &data);

    free_test_data(&data);

}

static void test_acpi_aarch64_virt_tcg_pxb(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };
    /*
     * While using -cdrom, the cdrom would auto plugged into pxb-pcie,
     * the reason is the bus of pxb-pcie is also root bus, it would lead
     * to the error only PCI/PCIE bridge could plug onto pxb.
     * Therefore,thr cdrom is defined and plugged onto the scsi controller
     * to solve the conflicts.
     */
    data.variant = ".pxb";
    test_acpi_one(" -device pcie-root-port,chassis=1,id=pci.1"
                  " -device virtio-scsi-pci,id=scsi0,bus=pci.1"
                  " -drive file="
                  "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2,"
                  "if=none,media=cdrom,id=drive-scsi0-0-0-1,readonly=on"
                  " -device scsi-cd,bus=scsi0.0,scsi-id=0,"
                  "drive=drive-scsi0-0-0-1,id=scsi0-0-0-1,bootindex=1"
                  " -cpu cortex-a57"
                  " -device pxb-pcie,bus_nr=128",
                  &data);

    free_test_data(&data);
}

static void test_acpi_aarch64_virt_tcg_acpi_spcr(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * 1024 * 1024,
        .variant = ".acpispcr",
    };

    test_acpi_one("-cpu cortex-a57 "
                  " -machine spcr=off", &data);
    free_test_data(&data);
}

static void test_acpi_riscv64_virt_tcg_acpi_spcr(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "riscv64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-riscv-code.fd",
        .uefi_fl2 = "pc-bios/edk2-riscv-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.riscv64.iso.qcow2",
        .ram_start = 0x80000000ULL,
        .scan_len = 128ULL * 1024 * 1024,
        .variant = ".acpispcr",
    };

    test_acpi_one("-cpu rva22s64 "
                  "-machine spcr=off", &data);
    free_test_data(&data);
}

static void test_acpi_tcg_acpi_hmat(const char *machine, const char *arch)
{
    test_data data = {};

    data.machine = machine;
    data.arch    = arch;
    data.variant = ".acpihmat";
    test_acpi_one(" -machine hmat=on"
                  " -smp 2,sockets=2"
                  " -m 128M,slots=2,maxmem=1G"
                  " -object memory-backend-ram,size=64M,id=m0"
                  " -object memory-backend-ram,size=64M,id=m1"
                  " -numa node,nodeid=0,memdev=m0"
                  " -numa node,nodeid=1,memdev=m1,initiator=0"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=0,socket-id=1"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=1"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=65534M"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-latency,latency=65534"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=32767M"
                  " -numa hmat-cache,node-id=0,size=10K,level=1,"
                  "associativity=direct,policy=write-back,line=8"
                  " -numa hmat-cache,node-id=1,size=10K,level=1,"
                  "associativity=direct,policy=write-back,line=8",
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_tcg_acpi_hmat(void)
{
    test_acpi_tcg_acpi_hmat(MACHINE_Q35, "x86");
}

static void test_acpi_piix4_tcg_acpi_hmat(void)
{
    test_acpi_tcg_acpi_hmat(MACHINE_PC, "x86");
}

static void test_acpi_aarch64_virt_tcg_acpi_hmat(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };

    data.variant = ".acpihmatvirt";

    test_acpi_one(" -machine hmat=on"
                  " -cpu cortex-a57"
                  " -smp 4,sockets=2"
                  " -m 384M"
                  " -object memory-backend-ram,size=128M,id=ram0"
                  " -object memory-backend-ram,size=128M,id=ram1"
                  " -object memory-backend-ram,size=128M,id=ram2"
                  " -numa node,nodeid=0,memdev=ram0"
                  " -numa node,nodeid=1,memdev=ram1"
                  " -numa node,nodeid=2,memdev=ram2"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=1,socket-id=1"
                  " -numa cpu,node-id=1,socket-id=1"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=10485760"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=5242880"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=30"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=1048576"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=5242880"
                  " -numa hmat-lb,initiator=1,target=1,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=1,target=1,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=10485760"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=30"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=1048576",
                  &data);

    free_test_data(&data);
}

static void test_acpi_q35_tcg_acpi_hmat_noinitiator(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86";
    data.variant = ".acpihmat-noinitiator";
    test_acpi_one(" -machine hmat=on"
                  " -smp 4,sockets=2"
                  " -m 128M"
                  " -object memory-backend-ram,size=32M,id=ram0"
                  " -object memory-backend-ram,size=32M,id=ram1"
                  " -object memory-backend-ram,size=64M,id=ram2"
                  " -numa node,nodeid=0,memdev=ram0"
                  " -numa node,nodeid=1,memdev=ram1"
                  " -numa node,nodeid=2,memdev=ram2"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=0,socket-id=0"
                  " -numa cpu,node-id=1,socket-id=1"
                  " -numa cpu,node-id=1,socket-id=1"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=10485760"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=0,target=1,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=5242880"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=30"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=1048576"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=5242880"
                  " -numa hmat-lb,initiator=1,target=1,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=1,target=1,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=10485760"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=30"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=1048576",
                  &data);
    free_test_data(&data);
}

/* Test intended to hit corner cases of SRAT and HMAT */
static void test_acpi_q35_tcg_acpi_hmat_generic_x(void)
{
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86";
    data.variant = ".acpihmat-generic-x";
    test_acpi_one(" -machine hmat=on,cxl=on"
                  " -smp 3,sockets=3"
                  " -m 128M,maxmem=384M,slots=2"
                  " -device pcie-root-port,chassis=1,id=pci.1"
                  " -device pci-testdev,bus=pci.1,"
                  "multifunction=on,addr=00.0"
                  " -device pci-testdev,bus=pci.1,addr=00.1"
                  " -device pci-testdev,bus=pci.1,id=gidev,addr=00.2"
                  " -device pxb-cxl,bus_nr=64,bus=pcie.0,id=cxl.1"
                  " -object memory-backend-ram,size=64M,id=ram0"
                  " -object memory-backend-ram,size=64M,id=ram1"
                  " -numa node,nodeid=0,cpus=0,memdev=ram0"
                  " -numa node,nodeid=1"
                  " -object acpi-generic-initiator,id=gi0,pci-dev=gidev,node=1"
                  " -numa node,nodeid=2"
                  " -object acpi-generic-port,id=gp0,pci-bus=cxl.1,node=2"
                  " -numa node,nodeid=3,cpus=1"
                  " -numa node,nodeid=4,memdev=ram1"
                  " -numa node,nodeid=5,cpus=2"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=0,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=800M"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=100"
                  " -numa hmat-lb,initiator=0,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=0,target=4,hierarchy=memory,"
                  "data-type=access-latency,latency=100"
                  " -numa hmat-lb,initiator=0,target=4,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=0,target=5,hierarchy=memory,"
                  "data-type=access-latency,latency=200"
                  " -numa hmat-lb,initiator=0,target=5,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=400M"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=500"
                  " -numa hmat-lb,initiator=1,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=100M"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=50"
                  " -numa hmat-lb,initiator=1,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=400M"
                  " -numa hmat-lb,initiator=1,target=4,hierarchy=memory,"
                  "data-type=access-latency,latency=50"
                  " -numa hmat-lb,initiator=1,target=4,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=800M"
                  " -numa hmat-lb,initiator=1,target=5,hierarchy=memory,"
                  "data-type=access-latency,latency=500"
                  " -numa hmat-lb,initiator=1,target=5,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=100M"
                  " -numa hmat-lb,initiator=3,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=3,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=400M"
                  " -numa hmat-lb,initiator=3,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=80"
                  " -numa hmat-lb,initiator=3,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=3,target=4,hierarchy=memory,"
                  "data-type=access-latency,latency=80"
                  " -numa hmat-lb,initiator=3,target=4,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=3,target=5,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=3,target=5,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=400M"
                  " -numa hmat-lb,initiator=5,target=0,hierarchy=memory,"
                  "data-type=access-latency,latency=20"
                  " -numa hmat-lb,initiator=5,target=0,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=400M"
                  " -numa hmat-lb,initiator=5,target=2,hierarchy=memory,"
                  "data-type=access-latency,latency=80"
                  " -numa hmat-lb,initiator=5,target=4,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=5,target=4,hierarchy=memory,"
                  "data-type=access-latency,latency=80"
                  " -numa hmat-lb,initiator=5,target=2,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=200M"
                  " -numa hmat-lb,initiator=5,target=5,hierarchy=memory,"
                  "data-type=access-latency,latency=10"
                  " -numa hmat-lb,initiator=5,target=5,hierarchy=memory,"
                  "data-type=access-bandwidth,bandwidth=800M",
                  &data);
    free_test_data(&data);
}

#ifdef CONFIG_POSIX
static void test_acpi_erst(const char *machine, const char *arch)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-erst.XXXXXX", NULL);
    gchar *params;
    test_data data = {};

    data.machine = machine;
    data.arch    = arch;
    data.variant = ".acpierst";
    params = g_strdup_printf(
        " -object memory-backend-file,id=erstnvram,"
            "mem-path=%s,size=0x10000,share=on"
        " -device acpi-erst,memdev=erstnvram", tmp_path);
    test_acpi_one(params, &data);
    free_test_data(&data);
    g_free(params);
    g_assert(g_rmdir(tmp_path) == 0);
    g_free(tmp_path);
}

static void test_acpi_piix4_acpi_erst(void)
{
    test_acpi_erst(MACHINE_PC, "x86");
}

static void test_acpi_q35_acpi_erst(void)
{
    test_acpi_erst(MACHINE_Q35, "x86");
}

static void test_acpi_microvm_acpi_erst(void)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-erst.XXXXXX", NULL);
    gchar *params;
    test_data data = {};

    test_acpi_microvm_prepare(&data);
    data.variant = ".pcie";
    data.tcg_only = true; /* need constant host-phys-bits */
    params = g_strdup_printf(" -machine microvm,"
        "acpi=on,ioapic2=off,rtc=off,pcie=on"
        " -object memory-backend-file,id=erstnvram,"
           "mem-path=%s,size=0x10000,share=on"
        " -device acpi-erst,memdev=erstnvram", tmp_path);
    test_acpi_one(params, &data);
    g_free(params);
    g_assert(g_rmdir(tmp_path) == 0);
    g_free(tmp_path);
    free_test_data(&data);
}
#endif /* CONFIG_POSIX */

static void test_acpi_riscv64_virt_tcg(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "riscv64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-riscv-code.fd",
        .uefi_fl2 = "pc-bios/edk2-riscv-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.riscv64.iso.qcow2",
        .ram_start = 0x80000000ULL,
        .scan_len = 128ULL * MiB,
    };

    /*
     * RHCT will have ISA string encoded. To reduce the effort
     * of updating expected AML file for any new default ISA extension,
     * use the profile rva22s64.
     */
    test_acpi_one("-cpu rva22s64 ", &data);
    free_test_data(&data);
}

static void test_acpi_aarch64_virt_tcg(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };

    data.smbios_cpu_max_speed = 2900;
    data.smbios_cpu_curr_speed = 2700;
    test_acpi_one("-cpu cortex-a57 "
                  "-smbios type=4,max-speed=2900,current-speed=2700", &data);
    free_test_data(&data);
}

static void test_acpi_aarch64_virt_tcg_topology(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .variant = ".topology",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };

    test_acpi_one("-cpu cortex-a57 "
                  "-smp sockets=1,clusters=2,cores=2,threads=2", &data);
    free_test_data(&data);
}

static void test_acpi_aarch64_virt_tcg_its_off(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .variant = ".its_off",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * 1024 * 1024,
    };

    test_acpi_one("-cpu cortex-a57 "
                  "-M gic-version=3,iommu=smmuv3,its=off", &data);
    free_test_data(&data);
}

static void test_acpi_q35_viot(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".viot",
    };

    /*
     * To keep things interesting, two buses bypass the IOMMU.
     * VIOT should only describes the other two buses.
     */
    test_acpi_one("-machine default_bus_bypass_iommu=on "
                  "-device virtio-iommu-pci "
                  "-device pxb-pcie,bus_nr=0x10,id=pcie.100,bus=pcie.0 "
                  "-device pxb-pcie,bus_nr=0x20,id=pcie.200,bus=pcie.0,bypass_iommu=on "
                  "-device pxb-pcie,bus_nr=0x30,id=pcie.300,bus=pcie.0",
                  &data);
    free_test_data(&data);
}

#ifdef CONFIG_POSIX
static void test_acpi_q35_cxl(void)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-cxl.XXXXXX", NULL);
    gchar *params;

    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".cxl",
    };
    /*
     * A complex CXL setup.
     */
    params = g_strdup_printf(" -machine cxl=on"
                             " -object memory-backend-file,id=cxl-mem1,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=cxl-mem2,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=cxl-mem3,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=cxl-mem4,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=lsa1,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=lsa2,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=lsa3,mem-path=%s,size=256M"
                             " -object memory-backend-file,id=lsa4,mem-path=%s,size=256M"
                             " -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1"
                             " -device pxb-cxl,bus_nr=222,bus=pcie.0,id=cxl.2"
                             " -device cxl-rp,port=0,bus=cxl.1,id=rp1,chassis=0,slot=2"
                             " -device cxl-type3,bus=rp1,persistent-memdev=cxl-mem1,lsa=lsa1"
                             " -device cxl-rp,port=1,bus=cxl.1,id=rp2,chassis=0,slot=3"
                             " -device cxl-type3,bus=rp2,persistent-memdev=cxl-mem2,lsa=lsa2"
                             " -device cxl-rp,port=0,bus=cxl.2,id=rp3,chassis=0,slot=5"
                             " -device cxl-type3,bus=rp3,persistent-memdev=cxl-mem3,lsa=lsa3"
                             " -device cxl-rp,port=1,bus=cxl.2,id=rp4,chassis=0,slot=6"
                             " -device cxl-type3,bus=rp4,persistent-memdev=cxl-mem4,lsa=lsa4"
                             " -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G,cxl-fmw.0.interleave-granularity=8k,"
                             "cxl-fmw.1.targets.0=cxl.1,cxl-fmw.1.targets.1=cxl.2,cxl-fmw.1.size=4G,cxl-fmw.1.interleave-granularity=8k",
                             tmp_path, tmp_path, tmp_path, tmp_path,
                             tmp_path, tmp_path, tmp_path, tmp_path);
    test_acpi_one(params, &data);

    g_free(params);
    g_assert(g_rmdir(tmp_path) == 0);
    g_free(tmp_path);
    free_test_data(&data);
}
#endif /* CONFIG_POSIX */

static void test_acpi_aarch64_virt_viot(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .variant = ".viot",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };

    test_acpi_one("-cpu cortex-a57 "
                  "-device virtio-iommu-pci", &data);
    free_test_data(&data);
}

#ifndef _WIN32
# define DEV_NULL "/dev/null"
#else
# define DEV_NULL "nul"
#endif

static void test_acpi_q35_slic(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".slic",
    };

    test_acpi_one("-acpitable sig=SLIC,oem_id=\"CRASH \",oem_table_id=ME,"
                  "oem_rev=00002210,asl_compiler_id=qemu,"
                  "asl_compiler_rev=00000000,data=" DEV_NULL,
                  &data);
    free_test_data(&data);
}

static void test_acpi_q35_applesmc(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".applesmc",
    };

    /* supply fake 64-byte OSK to silence missing key warning */
    test_acpi_one("-device isa-applesmc,osk=any64characterfakeoskisenough"
                  "topreventinvalidkeywarningsonstderr", &data);
    free_test_data(&data);
}

static void test_acpi_q35_pvpanic_isa(void)
{
    test_data data = {
        .machine = MACHINE_Q35,
        .arch    = "x86",
        .variant = ".pvpanic-isa",
    };

    test_acpi_one("-device pvpanic", &data);
    free_test_data(&data);
}

static void test_acpi_pc_smbios_options(void)
{
    uint8_t req_type11[] = { 11 };
    test_data data = {
        .machine = MACHINE_PC,
        .arch    = "x86",
        .variant = ".pc_smbios_options",
        .required_struct_types = req_type11,
        .required_struct_types_len = ARRAY_SIZE(req_type11),
    };

    test_smbios("-smbios type=11,value=TEST", &data);
    free_test_data(&data);
}

static void test_acpi_pc_smbios_blob(void)
{
    uint8_t req_type11[] = { 11 };
    test_data data = {
        .machine = MACHINE_PC,
        .arch    = "x86",
        .variant = ".pc_smbios_blob",
        .required_struct_types = req_type11,
        .required_struct_types_len = ARRAY_SIZE(req_type11),
    };

    test_smbios("-machine smbios-entry-point-type=32 "
                "-smbios file=tests/data/smbios/type11_blob", &data);
    free_test_data(&data);
}

static void test_acpi_isapc_smbios_legacy(void)
{
    uint8_t req_type11[] = { 1, 11 };
    test_data data = {
        .machine = "isapc",
        .variant = ".pc_smbios_legacy",
        .required_struct_types = req_type11,
        .required_struct_types_len = ARRAY_SIZE(req_type11),
    };

    test_smbios("-smbios file=tests/data/smbios/type11_blob.legacy "
                "-smbios type=1,family=TEST", &data);
    free_test_data(&data);
}

static void test_oem_fields(test_data *data)
{
    int i;

    for (i = 0; i < data->tables->len; ++i) {
        AcpiSdtTable *sdt;

        sdt = &g_array_index(data->tables, AcpiSdtTable, i);
        /* FACS doesn't have OEMID and OEMTABLEID fields */
        if (compare_signature(sdt, "FACS")) {
            continue;
        }

        g_assert(strncmp((char *)sdt->aml + 10, OEM_ID, 6) == 0);
        g_assert(strncmp((char *)sdt->aml + 16, OEM_TABLE_ID, 8) == 0);
    }
}

static void test_acpi_piix4_oem_fields(void)
{
    char *args;
    test_data data = {};

    data.machine = MACHINE_PC;
    data.arch    = "x86";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);

    args = test_acpi_create_args(&data, OEM_TEST_ARGS);
    data.qts = qtest_init(args);
    test_acpi_load_tables(&data);
    test_oem_fields(&data);
    qtest_quit(data.qts);
    free_test_data(&data);
    g_free(args);
}

static void test_acpi_q35_oem_fields(void)
{
    char *args;
    test_data data = {};

    data.machine = MACHINE_Q35;
    data.arch    = "x86";
    data.required_struct_types = base_required_struct_types;
    data.required_struct_types_len = ARRAY_SIZE(base_required_struct_types);

    args = test_acpi_create_args(&data, OEM_TEST_ARGS);
    data.qts = qtest_init(args);
    test_acpi_load_tables(&data);
    test_oem_fields(&data);
    qtest_quit(data.qts);
    free_test_data(&data);
    g_free(args);
}

static void test_acpi_microvm_oem_fields(void)
{
    test_data data = {};
    char *args;

    test_acpi_microvm_prepare(&data);

    args = test_acpi_create_args(&data,
                                 OEM_TEST_ARGS",acpi=on");
    data.qts = qtest_init(args);
    test_acpi_load_tables(&data);
    test_oem_fields(&data);
    qtest_quit(data.qts);
    free_test_data(&data);
    g_free(args);
}

static void test_acpi_aarch64_virt_oem_fields(void)
{
    test_data data = {
        .machine = "virt",
        .arch = "aarch64",
        .tcg_only = true,
        .uefi_fl1 = "pc-bios/edk2-aarch64-code.fd",
        .uefi_fl2 = "pc-bios/edk2-arm-vars.fd",
        .cd = "tests/data/uefi-boot-images/bios-tables-test.aarch64.iso.qcow2",
        .ram_start = 0x40000000ULL,
        .scan_len = 128ULL * MiB,
    };
    char *args;

    args = test_acpi_create_args(&data, "-cpu cortex-a57 "OEM_TEST_ARGS);
    data.qts = qtest_init(args);
    test_acpi_load_tables(&data);
    test_oem_fields(&data);
    qtest_quit(data.qts);
    free_test_data(&data);
    g_free(args);
}

#define LOONGARCH64_INIT_TEST_DATA(data)                          \
    test_data data = {                                            \
        .machine = "virt",                                        \
        .arch    = "loongarch64",                                 \
        .tcg_only = true,                                         \
        .uefi_fl1 = "pc-bios/edk2-loongarch64-code.fd",           \
        .uefi_fl2 = "pc-bios/edk2-loongarch64-vars.fd",           \
        .cd = "tests/data/uefi-boot-images/"                      \
              "bios-tables-test.loongarch64.iso.qcow2",           \
        .ram_start = 0,                                           \
        .scan_len = 128ULL * MiB,                                 \
    }

static void test_acpi_loongarch64_virt(void)
{
    LOONGARCH64_INIT_TEST_DATA(data);

    test_acpi_one("-cpu la464 ", &data);
    free_test_data(&data);
}

static void test_acpi_loongarch64_virt_topology(void)
{
    LOONGARCH64_INIT_TEST_DATA(data);

    data.variant = ".topology";
    test_acpi_one("-cpu la464 -smp sockets=1,cores=2,threads=2", &data);
    free_test_data(&data);
}

static void test_acpi_loongarch64_virt_numamem(void)
{
    LOONGARCH64_INIT_TEST_DATA(data);

    data.variant = ".numamem";
    test_acpi_one(" -cpu la464 -m 128"
                  " -object memory-backend-ram,id=ram0,size=64M"
                  " -object memory-backend-ram,id=ram1,size=64M"
                  " -numa node,memdev=ram0 -numa node,memdev=ram1"
                  " -numa dist,src=0,dst=1,val=21",
                  &data);
    free_test_data(&data);
}

static void test_acpi_loongarch64_virt_memhp(void)
{
    LOONGARCH64_INIT_TEST_DATA(data);

    data.variant = ".memhp";
    test_acpi_one(" -cpu la464 -m 128,slots=2,maxmem=256M"
                  " -object memory-backend-ram,id=ram0,size=128M",
                  &data);
    free_test_data(&data);
}

static void test_acpi_loongarch64_virt_oem_fields(void)
{
    LOONGARCH64_INIT_TEST_DATA(data);
    char *args;

    args = test_acpi_create_args(&data, "-cpu la464 "OEM_TEST_ARGS);
    data.qts = qtest_init(args);
    test_acpi_load_tables(&data);
    test_oem_fields(&data);
    qtest_quit(data.qts);
    free_test_data(&data);
    g_free(args);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    bool has_kvm, has_tcg;
    char *v_env = getenv("V");
    int ret;

    if (v_env) {
        verbosity_level = atoi(v_env);
    }

    g_test_init(&argc, &argv, NULL);

    has_kvm = qtest_has_accel("kvm");
    has_tcg = qtest_has_accel("tcg");

    if (!has_tcg && !has_kvm) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        ret = boot_sector_init(disk);
        if (ret) {
            return ret;
        }
        if (qtest_has_machine(MACHINE_PC)) {
            qtest_add_func("acpi/piix4", test_acpi_piix4_tcg);
            qtest_add_func("acpi/piix4/oem-fields", test_acpi_piix4_oem_fields);
            qtest_add_func("acpi/piix4/bridge", test_acpi_piix4_tcg_bridge);
            qtest_add_func("acpi/piix4/pci-hotplug/no_root_hotplug",
                           test_acpi_piix4_no_root_hotplug);
            qtest_add_func("acpi/piix4/pci-hotplug/no_bridge_hotplug",
                           test_acpi_piix4_no_bridge_hotplug);
            qtest_add_func("acpi/piix4/pci-hotplug/off",
                           test_acpi_piix4_no_acpi_pci_hotplug);
            qtest_add_func("acpi/piix4/ipmi", test_acpi_piix4_tcg_ipmi);
            qtest_add_func("acpi/piix4/cpuhp", test_acpi_piix4_tcg_cphp);
            qtest_add_func("acpi/piix4/numamem", test_acpi_piix4_tcg_numamem);
            qtest_add_func("acpi/piix4/nosmm", test_acpi_piix4_tcg_nosmm);
            qtest_add_func("acpi/piix4/smm-compat",
                           test_acpi_piix4_tcg_smm_compat);
            qtest_add_func("acpi/piix4/smm-compat-nosmm",
                           test_acpi_piix4_tcg_smm_compat_nosmm);
            qtest_add_func("acpi/piix4/nohpet", test_acpi_piix4_tcg_nohpet);

            /* i386 does not support memory hotplug */
            if (strcmp(arch, "i386")) {
                qtest_add_func("acpi/piix4/memhp", test_acpi_piix4_tcg_memhp);
                qtest_add_func("acpi/piix4/dimmpxm",
                               test_acpi_piix4_tcg_dimm_pxm);
                qtest_add_func("acpi/piix4/acpihmat",
                               test_acpi_piix4_tcg_acpi_hmat);
            }
#ifdef CONFIG_POSIX
            qtest_add_func("acpi/piix4/acpierst", test_acpi_piix4_acpi_erst);
#endif
            qtest_add_func("acpi/piix4/smbios-options",
                           test_acpi_pc_smbios_options);
            qtest_add_func("acpi/piix4/smbios-blob",
                           test_acpi_pc_smbios_blob);
            qtest_add_func("acpi/piix4/smbios-legacy",
                           test_acpi_isapc_smbios_legacy);
        }
        if (qtest_has_machine(MACHINE_Q35)) {
            qtest_add_func("acpi/q35", test_acpi_q35_tcg);
            qtest_add_func("acpi/q35/oem-fields", test_acpi_q35_oem_fields);
            if (tpm_model_is_available("-machine q35", "tpm-tis")) {
                qtest_add_func("acpi/q35/tpm2-tis", test_acpi_q35_tcg_tpm2_tis);
                qtest_add_func("acpi/q35/tpm12-tis",
                               test_acpi_q35_tcg_tpm12_tis);
            }
            qtest_add_func("acpi/q35/bridge", test_acpi_q35_tcg_bridge);
            qtest_add_func("acpi/q35/no-acpi-hotplug",
                           test_acpi_q35_tcg_no_acpi_hotplug);
            qtest_add_func("acpi/q35/multif-bridge",
                           test_acpi_q35_multif_bridge);
            qtest_add_func("acpi/q35/ipmi", test_acpi_q35_tcg_ipmi);
            qtest_add_func("acpi/q35/smbus/ipmi", test_acpi_q35_tcg_smbus_ipmi);
            qtest_add_func("acpi/q35/cpuhp", test_acpi_q35_tcg_cphp);
            qtest_add_func("acpi/q35/numamem", test_acpi_q35_tcg_numamem);
            qtest_add_func("acpi/q35/nosmm", test_acpi_q35_tcg_nosmm);
            qtest_add_func("acpi/q35/smm-compat",
                           test_acpi_q35_tcg_smm_compat);
            qtest_add_func("acpi/q35/smm-compat-nosmm",
                           test_acpi_q35_tcg_smm_compat_nosmm);
            qtest_add_func("acpi/q35/nohpet", test_acpi_q35_tcg_nohpet);
            qtest_add_func("acpi/q35/acpihmat-noinitiator",
                           test_acpi_q35_tcg_acpi_hmat_noinitiator);
            qtest_add_func("acpi/q35/acpihmat-genericx",
                           test_acpi_q35_tcg_acpi_hmat_generic_x);

            /* i386 does not support memory hotplug */
            if (strcmp(arch, "i386")) {
                qtest_add_func("acpi/q35/memhp", test_acpi_q35_tcg_memhp);
                qtest_add_func("acpi/q35/dimmpxm", test_acpi_q35_tcg_dimm_pxm);
                qtest_add_func("acpi/q35/acpihmat",
                               test_acpi_q35_tcg_acpi_hmat);
                qtest_add_func("acpi/q35/mmio64", test_acpi_q35_tcg_mmio64);
            }
#ifdef CONFIG_POSIX
            qtest_add_func("acpi/q35/acpierst", test_acpi_q35_acpi_erst);
#endif
            qtest_add_func("acpi/q35/applesmc", test_acpi_q35_applesmc);
            qtest_add_func("acpi/q35/pvpanic-isa", test_acpi_q35_pvpanic_isa);
            if (has_tcg) {
                qtest_add_func("acpi/q35/ivrs", test_acpi_q35_tcg_ivrs);
            }
            if (has_kvm) {
                qtest_add_func("acpi/q35/kvm/xapic", test_acpi_q35_kvm_xapic);
                qtest_add_func("acpi/q35/kvm/dmar", test_acpi_q35_kvm_dmar);
                qtest_add_func("acpi/q35/type4-count",
                               test_acpi_q35_kvm_type4_count);
                qtest_add_func("acpi/q35/core-count",
                               test_acpi_q35_kvm_core_count);
                qtest_add_func("acpi/q35/core-count2",
                               test_acpi_q35_kvm_core_count2);
                qtest_add_func("acpi/q35/thread-count",
                               test_acpi_q35_kvm_thread_count);
                qtest_add_func("acpi/q35/thread-count2",
                               test_acpi_q35_kvm_thread_count2);
            }
            if (qtest_has_device("virtio-iommu-pci")) {
                qtest_add_func("acpi/q35/viot", test_acpi_q35_viot);
            }
#ifdef CONFIG_POSIX
            qtest_add_func("acpi/q35/cxl", test_acpi_q35_cxl);
#endif
            qtest_add_func("acpi/q35/slic", test_acpi_q35_slic);
        }
        if (qtest_has_machine("microvm")) {
            qtest_add_func("acpi/microvm", test_acpi_microvm_tcg);
            qtest_add_func("acpi/microvm/usb", test_acpi_microvm_usb_tcg);
            qtest_add_func("acpi/microvm/rtc", test_acpi_microvm_rtc_tcg);
            qtest_add_func("acpi/microvm/ioapic2",
                           test_acpi_microvm_ioapic2_tcg);
            qtest_add_func("acpi/microvm/oem-fields",
                           test_acpi_microvm_oem_fields);
            if (has_tcg) {
                if (strcmp(arch, "x86_64") == 0) {
                    qtest_add_func("acpi/microvm/pcie",
                                   test_acpi_microvm_pcie_tcg);
#ifdef CONFIG_POSIX
                    qtest_add_func("acpi/microvm/acpierst",
                                   test_acpi_microvm_acpi_erst);
#endif
                }
            }
        }
    } else if (strcmp(arch, "aarch64") == 0) {
        if (has_tcg && qtest_has_device("virtio-blk-pci")) {
            qtest_add_func("acpi/virt", test_acpi_aarch64_virt_tcg);
            qtest_add_func("acpi/virt/acpihmatvirt",
                           test_acpi_aarch64_virt_tcg_acpi_hmat);
            qtest_add_func("acpi/virt/topology",
                           test_acpi_aarch64_virt_tcg_topology);
            qtest_add_func("acpi/virt/its_off",
                           test_acpi_aarch64_virt_tcg_its_off);
            qtest_add_func("acpi/virt/numamem",
                           test_acpi_aarch64_virt_tcg_numamem);
            qtest_add_func("acpi/virt/memhp", test_acpi_aarch64_virt_tcg_memhp);
            qtest_add_func("acpi/virt/acpipcihp",
                           test_acpi_aarch64_virt_acpi_pci_hotplug);
            qtest_add_func("acpi/virt/hpoffacpiindex",
                          test_acpi_aarch64_virt_pcie_root_port_hpoff);
            qtest_add_func("acpi/virt/pxb", test_acpi_aarch64_virt_tcg_pxb);
            qtest_add_func("acpi/virt/oem-fields",
                           test_acpi_aarch64_virt_oem_fields);
            qtest_add_func("acpi/virt/acpispcr",
                           test_acpi_aarch64_virt_tcg_acpi_spcr);
            if (qtest_has_device("virtio-iommu-pci")) {
                qtest_add_func("acpi/virt/viot", test_acpi_aarch64_virt_viot);
            }
        }
    } else if (strcmp(arch, "riscv64") == 0) {
        if (has_tcg && qtest_has_device("virtio-blk-pci")) {
            qtest_add_func("acpi/virt", test_acpi_riscv64_virt_tcg);
            qtest_add_func("acpi/virt/numamem",
                           test_acpi_riscv64_virt_tcg_numamem);
            qtest_add_func("acpi/virt/acpispcr",
                           test_acpi_riscv64_virt_tcg_acpi_spcr);
        }
    } else if (strcmp(arch, "loongarch64") == 0) {
        if (has_tcg) {
            qtest_add_func("acpi/virt", test_acpi_loongarch64_virt);
            qtest_add_func("acpi/virt/topology",
                           test_acpi_loongarch64_virt_topology);
            qtest_add_func("acpi/virt/numamem",
                           test_acpi_loongarch64_virt_numamem);
            qtest_add_func("acpi/virt/memhp", test_acpi_loongarch64_virt_memhp);
            qtest_add_func("acpi/virt/oem-fields",
                           test_acpi_loongarch64_virt_oem_fields);
        }
    }
    ret = g_test_run();
    boot_sector_cleanup(disk);
    return ret;
}
