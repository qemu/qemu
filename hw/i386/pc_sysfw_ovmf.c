/*
 * QEMU PC System Firmware (OVMF specific)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2011-2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/i386/pc.h"
#include "cpu.h"

#define OVMF_TABLE_FOOTER_GUID "96b582de-1fb2-45f7-baea-a366c55a082d"

static const int bytes_after_table_footer = 32;

static bool ovmf_flash_parsed;
static uint8_t *ovmf_table;
static int ovmf_table_len;

void pc_system_parse_ovmf_flash(uint8_t *flash_ptr, size_t flash_size)
{
    uint8_t *ptr;
    QemuUUID guid;
    int tot_len;

    /* should only be called once */
    if (ovmf_flash_parsed) {
        return;
    }

    ovmf_flash_parsed = true;

    if (flash_size < TARGET_PAGE_SIZE) {
        return;
    }

    /*
     * if this is OVMF there will be a table footer
     * guid 48 bytes before the end of the flash file
     * (= 32 bytes after the table + 16 bytes the GUID itself).
     * If it's not found, silently abort the flash parsing.
     */
    qemu_uuid_parse(OVMF_TABLE_FOOTER_GUID, &guid);
    guid = qemu_uuid_bswap(guid); /* guids are LE */
    ptr = flash_ptr + flash_size - (bytes_after_table_footer + sizeof(guid));
    if (!qemu_uuid_is_equal((QemuUUID *)ptr, &guid)) {
        return;
    }

    /* if found, just before is two byte table length */
    ptr -= sizeof(uint16_t);
    tot_len = le16_to_cpu(*(uint16_t *)ptr) - sizeof(guid) - sizeof(uint16_t);

    if (tot_len < 0 || tot_len > (ptr - flash_ptr)) {
        error_report("OVMF table has invalid size %d", tot_len);
        return;
    }

    if (tot_len == 0) {
        /* no entries in the OVMF table */
        return;
    }

    ovmf_table = g_malloc(tot_len);
    ovmf_table_len = tot_len;

    /*
     * ptr is the foot of the table, so copy it all to the newly
     * allocated ovmf_table and then set the ovmf_table pointer
     * to the table foot
     */
    memcpy(ovmf_table, ptr - tot_len, tot_len);
    ovmf_table += tot_len;
}

/**
 * pc_system_ovmf_table_find - Find the data associated with an entry in OVMF's
 * reset vector GUIDed table.
 *
 * @entry: GUID string of the entry to lookup
 * @data: Filled with a pointer to the entry's value (if not NULL)
 * @data_len: Filled with the length of the entry's value (if not NULL). Pass
 *            NULL here if the length of data is known.
 *
 * Return: true if the entry was found in the OVMF table; false otherwise.
 */
bool pc_system_ovmf_table_find(const char *entry, uint8_t **data,
                               int *data_len)
{
    uint8_t *ptr = ovmf_table;
    int tot_len = ovmf_table_len;
    QemuUUID entry_guid;

    assert(ovmf_flash_parsed);

    if (qemu_uuid_parse(entry, &entry_guid) < 0) {
        return false;
    }

    if (!ptr) {
        return false;
    }

    entry_guid = qemu_uuid_bswap(entry_guid); /* guids are LE */
    while (tot_len >= sizeof(QemuUUID) + sizeof(uint16_t)) {
        int len;
        QemuUUID *guid;

        /*
         * The data structure is
         *   arbitrary length data
         *   2 byte length of entire entry
         *   16 byte guid
         */
        guid = (QemuUUID *)(ptr - sizeof(QemuUUID));
        len = le16_to_cpu(*(uint16_t *)(ptr - sizeof(QemuUUID) -
                                        sizeof(uint16_t)));

        /*
         * just in case the table is corrupt, wouldn't want to spin in
         * the zero case
         */
        if (len < sizeof(QemuUUID) + sizeof(uint16_t)) {
            return false;
        } else if (len > tot_len) {
            return false;
        }

        ptr -= len;
        tot_len -= len;
        if (qemu_uuid_is_equal(guid, &entry_guid)) {
            if (data) {
                *data = ptr;
            }
            if (data_len) {
                *data_len = len - sizeof(QemuUUID) - sizeof(uint16_t);
            }
            return true;
        }
    }
    return false;
}
