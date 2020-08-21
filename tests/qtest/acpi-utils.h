/*
 * Utilities for working with ACPI tables
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TEST_ACPI_UTILS_H
#define TEST_ACPI_UTILS_H

#include "libqos/libqtest.h"

/* DSDT and SSDTs format */
typedef struct {
    uint8_t *aml;            /* aml bytecode from guest */
    uint32_t aml_len;
    gchar *aml_file;
    gchar *asl;            /* asl code generated from aml */
    gsize asl_len;
    gchar *asl_file;
    bool tmp_files_retain;   /* do not delete the temp asl/aml */
} AcpiSdtTable;

#define ACPI_ASSERT_CMP(actual, expected) do { \
    char ACPI_ASSERT_CMP_str[5] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &actual, 4); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)

#define ACPI_ASSERT_CMP64(actual, expected) do { \
    char ACPI_ASSERT_CMP_str[9] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &actual, 8); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)


#define ACPI_FOREACH_RSDT_ENTRY(table, table_len, entry_ptr, entry_size) \
    for (entry_ptr = table + 36 /* 1st Entry */;                         \
         entry_ptr < table + table_len;                                  \
         entry_ptr += entry_size)

uint8_t acpi_calc_checksum(const uint8_t *data, int len);
uint32_t acpi_find_rsdp_address(QTestState *qts);
uint64_t acpi_find_rsdp_address_uefi(QTestState *qts, uint64_t start,
                                     uint64_t size);
void acpi_fetch_rsdp_table(QTestState *qts, uint64_t addr, uint8_t *rsdp_table);
void acpi_fetch_table(QTestState *qts, uint8_t **aml, uint32_t *aml_len,
                      const uint8_t *addr_ptr, int addr_size, const char *sig,
                      bool verify_checksum);

#endif /* TEST_ACPI_UTILS_H */
