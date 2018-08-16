/*
 * QTest testcase for VM Generation ID
 *
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2017 Skyport Systems
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/uuid.h"
#include "hw/acpi/acpi-defs.h"
#include "boot-sector.h"
#include "acpi-utils.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"

#define VGID_GUID "324e6eaf-d1d1-4bf6-bf41-b9bb6c91fb87"
#define VMGENID_GUID_OFFSET 40   /* allow space for
                                  * OVMF SDT Header Probe Supressor
                                  */
#define RSDP_ADDR_INVALID 0x100000 /* RSDP must be below this address */

typedef struct {
    AcpiTableHeader header;
    gchar name_op;
    gchar vgia[4];
    gchar val_op;
    uint32_t vgia_val;
} QEMU_PACKED VgidTable;

static uint32_t acpi_find_vgia(void)
{
    uint32_t rsdp_offset;
    uint32_t guid_offset = 0;
    AcpiRsdpDescriptor rsdp_table;
    uint32_t rsdt, rsdt_table_length;
    AcpiRsdtDescriptorRev1 rsdt_table;
    size_t tables_nr;
    uint32_t *tables;
    AcpiTableHeader ssdt_table;
    VgidTable vgid_table;
    int i;

    /* Wait for guest firmware to finish and start the payload. */
    boot_sector_test(global_qtest);

    /* Tables should be initialized now. */
    rsdp_offset = acpi_find_rsdp_address();

    g_assert_cmphex(rsdp_offset, <, RSDP_ADDR_INVALID);

    acpi_parse_rsdp_table(rsdp_offset, &rsdp_table);

    rsdt = le32_to_cpu(rsdp_table.rsdt_physical_address);
    /* read the header */
    ACPI_READ_TABLE_HEADER(&rsdt_table, rsdt);
    ACPI_ASSERT_CMP(rsdt_table.signature, "RSDT");
    rsdt_table_length = le32_to_cpu(rsdt_table.length);

    /* compute the table entries in rsdt */
    g_assert_cmpint(rsdt_table_length, >, sizeof(AcpiRsdtDescriptorRev1));
    tables_nr = (rsdt_table_length - sizeof(AcpiRsdtDescriptorRev1)) /
                sizeof(uint32_t);

    /* get the addresses of the tables pointed by rsdt */
    tables = g_new0(uint32_t, tables_nr);
    ACPI_READ_ARRAY_PTR(tables, tables_nr, rsdt);

    for (i = 0; i < tables_nr; i++) {
        uint32_t addr = le32_to_cpu(tables[i]);
        ACPI_READ_TABLE_HEADER(&ssdt_table, addr);
        if (!strncmp((char *)ssdt_table.oem_table_id, "VMGENID", 7)) {
            /* the first entry in the table should be VGIA
             * That's all we need
             */
            ACPI_READ_FIELD(vgid_table.name_op, addr);
            g_assert(vgid_table.name_op == 0x08);  /* name */
            ACPI_READ_ARRAY(vgid_table.vgia, addr);
            g_assert(memcmp(vgid_table.vgia, "VGIA", 4) == 0);
            ACPI_READ_FIELD(vgid_table.val_op, addr);
            g_assert(vgid_table.val_op == 0x0C);  /* dword */
            ACPI_READ_FIELD(vgid_table.vgia_val, addr);
            /* The GUID is written at a fixed offset into the fw_cfg file
             * in order to implement the "OVMF SDT Header probe suppressor"
             * see docs/specs/vmgenid.txt for more details
             */
            guid_offset = le32_to_cpu(vgid_table.vgia_val) + VMGENID_GUID_OFFSET;
            break;
        }
    }
    g_free(tables);
    return guid_offset;
}

static void read_guid_from_memory(QemuUUID *guid)
{
    uint32_t vmgenid_addr;
    int i;

    vmgenid_addr = acpi_find_vgia();
    g_assert(vmgenid_addr);

    /* Read the GUID directly from guest memory */
    for (i = 0; i < 16; i++) {
        guid->data[i] = readb(vmgenid_addr + i);
    }
    /* The GUID is in little-endian format in the guest, while QEMU
     * uses big-endian.  Swap after reading.
     */
    qemu_uuid_bswap(guid);
}

static void read_guid_from_monitor(QemuUUID *guid)
{
    QDict *rsp, *rsp_ret;
    const char *guid_str;

    rsp = qmp("{ 'execute': 'query-vm-generation-id' }");
    if (qdict_haskey(rsp, "return")) {
        rsp_ret = qdict_get_qdict(rsp, "return");
        g_assert(qdict_haskey(rsp_ret, "guid"));
        guid_str = qdict_get_str(rsp_ret, "guid");
        g_assert(qemu_uuid_parse(guid_str, guid) == 0);
    }
    qobject_unref(rsp);
}

static char disk[] = "tests/vmgenid-test-disk-XXXXXX";

#define GUID_CMD(guid)                          \
    "-machine accel=kvm:tcg "                   \
    "-device vmgenid,id=testvgid,guid=%s "      \
    "-drive id=hd0,if=none,file=%s,format=raw " \
    "-device ide-hd,drive=hd0 ", guid, disk

static void vmgenid_set_guid_test(void)
{
    QemuUUID expected, measured;

    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);

    global_qtest = qtest_initf(GUID_CMD(VGID_GUID));

    /* Read the GUID from accessing guest memory */
    read_guid_from_memory(&measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);

    qtest_quit(global_qtest);
}

static void vmgenid_set_guid_auto_test(void)
{
    QemuUUID measured;

    global_qtest = qtest_initf(GUID_CMD("auto"));

    read_guid_from_memory(&measured);

    /* Just check that the GUID is non-null */
    g_assert(!qemu_uuid_is_null(&measured));

    qtest_quit(global_qtest);
}

static void vmgenid_query_monitor_test(void)
{
    QemuUUID expected, measured;

    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);

    global_qtest = qtest_initf(GUID_CMD(VGID_GUID));

    /* Read the GUID via the monitor */
    read_guid_from_monitor(&measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);

    qtest_quit(global_qtest);
}

int main(int argc, char **argv)
{
    int ret;

    ret = boot_sector_init(disk);
    if (ret) {
        return ret;
    }

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/vmgenid/vmgenid/set-guid",
                   vmgenid_set_guid_test);
    qtest_add_func("/vmgenid/vmgenid/set-guid-auto",
                   vmgenid_set_guid_auto_test);
    qtest_add_func("/vmgenid/vmgenid/query-monitor",
                   vmgenid_query_monitor_test);
    ret = g_test_run();
    boot_sector_cleanup(disk);

    return ret;
}
