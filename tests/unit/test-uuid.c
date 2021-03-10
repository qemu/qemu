/*
 * QEMU UUID Library
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/uuid.h"

struct {
    const char *uuidstr;
    QemuUUID uuid;
    bool uuidstr_is_valid;
    bool check_unparse;
} uuid_test_data[] = {
    {    /* Normal */
        "586ece27-7f09-41e0-9e74-e901317e9d42",
        { { {
             0x58, 0x6e, 0xce, 0x27, 0x7f, 0x09, 0x41, 0xe0,
             0x9e, 0x74, 0xe9, 0x01, 0x31, 0x7e, 0x9d, 0x42,
        } } },
        true, true,
    }, { /* NULL */
        "00000000-0000-0000-0000-000000000000",
        { },
        true, true,
    }, { /* Upper case */
        "0CC6C752-3961-4028-A286-C05CC616D396",
        { { {
             0x0c, 0xc6, 0xc7, 0x52, 0x39, 0x61, 0x40, 0x28,
             0xa2, 0x86, 0xc0, 0x5c, 0xc6, 0x16, 0xd3, 0x96,
        } } },
        true, false,
    }, { /* Mixed case */
        "0CC6C752-3961-4028-a286-c05cc616D396",
        { { {
             0x0c, 0xc6, 0xc7, 0x52, 0x39, 0x61, 0x40, 0x28,
             0xa2, 0x86, 0xc0, 0x5c, 0xc6, 0x16, 0xd3, 0x96,
        } } },
        true, false,
    }, { /* Empty */
        ""
    }, { /* Too short */
        "abc",
    }, { /* Non-hex */
        "abcdefgh-0000-0000-0000-000000000000",
    }, { /* No '-' */
        "0cc6c75239614028a286c05cc616d396",
    }, { /* '-' in wrong position */
        "0cc6c-7523961-4028-a286-c05cc616d396",
    }, { /* Double '-' */
        "0cc6c752--3961-4028-a286-c05cc616d396",
    }, { /* Too long */
        "0000000000000000000000000000000000000000000000",
    }, { /* Invalid char in the beginning */
        ")cc6c752-3961-4028-a286-c05cc616d396",
    }, { /* Invalid char in the beginning, in extra */
        ")0cc6c752-3961-4028-a286-c05cc616d396",
    }, { /* Invalid char in the middle */
        "0cc6c752-39*1-4028-a286-c05cc616d396",
    }, { /* Invalid char in the middle, in extra */
        "0cc6c752-39*61-4028-a286-c05cc616d396",
    }, { /* Invalid char in the end */
        "0cc6c752-3961-4028-a286-c05cc616d39&",
    }, { /* Invalid char in the end, in extra */
        "0cc6c752-3961-4028-a286-c05cc616d396&",
    }, { /* Short end and trailing space */
        "0cc6c752-3961-4028-a286-c05cc616d39 ",
    }, { /* Leading space and short end */
        " 0cc6c752-3961-4028-a286-c05cc616d39",
    },
};

static inline bool uuid_is_valid(QemuUUID *uuid)
{
    return qemu_uuid_is_null(uuid) ||
            ((uuid->data[6] & 0xf0) == 0x40 && (uuid->data[8] & 0xc0) == 0x80);
}

static void test_uuid_generate(void)
{
    QemuUUID uuid_not_null = { { {
        0x58, 0x6e, 0xce, 0x27, 0x7f, 0x09, 0x41, 0xe0,
        0x9e, 0x74, 0xe9, 0x01, 0x31, 0x7e, 0x9d, 0x42
    } } };
    QemuUUID uuid;
    int i;

    for (i = 0; i < 100; ++i) {
        qemu_uuid_generate(&uuid);
        g_assert(uuid_is_valid(&uuid));
        g_assert_false(qemu_uuid_is_null(&uuid));
        g_assert_false(qemu_uuid_is_equal(&uuid_not_null, &uuid));
    }
}

static void test_uuid_is_null(void)
{
    QemuUUID uuid_null = { };
    QemuUUID uuid_not_null = { { {
        0x58, 0x6e, 0xce, 0x27, 0x7f, 0x09, 0x41, 0xe0,
        0x9e, 0x74, 0xe9, 0x01, 0x31, 0x7e, 0x9d, 0x42
    } } };
    QemuUUID uuid_not_null_2 = { { { 1 } } };

    g_assert(qemu_uuid_is_null(&uuid_null));
    g_assert_false(qemu_uuid_is_null(&uuid_not_null));
    g_assert_false(qemu_uuid_is_null(&uuid_not_null_2));
}

static void test_uuid_parse(void)
{
    int i, r;

    for (i = 0; i < ARRAY_SIZE(uuid_test_data); i++) {
        QemuUUID uuid;
        bool is_valid = uuid_test_data[i].uuidstr_is_valid;

        r = qemu_uuid_parse(uuid_test_data[i].uuidstr, &uuid);
        g_assert_cmpint(!r, ==, is_valid);
        if (is_valid) {
            g_assert_cmpint(is_valid, ==, uuid_is_valid(&uuid));
            g_assert_cmpmem(&uuid_test_data[i].uuid, sizeof(uuid),
                            &uuid, sizeof(uuid));
        }
    }
}

static void test_uuid_unparse(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(uuid_test_data); i++) {
        char out[37];

        if (!uuid_test_data[i].check_unparse) {
            continue;
        }
        qemu_uuid_unparse(&uuid_test_data[i].uuid, out);
        g_assert_cmpstr(uuid_test_data[i].uuidstr, ==, out);
    }
}

static void test_uuid_unparse_strdup(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(uuid_test_data); i++) {
        char *out;

        if (!uuid_test_data[i].check_unparse) {
            continue;
        }
        out = qemu_uuid_unparse_strdup(&uuid_test_data[i].uuid);
        g_assert_cmpstr(uuid_test_data[i].uuidstr, ==, out);
        g_free(out);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/uuid/is_null", test_uuid_is_null);
    g_test_add_func("/uuid/generate", test_uuid_generate);
    g_test_add_func("/uuid/parse", test_uuid_parse);
    g_test_add_func("/uuid/unparse", test_uuid_unparse);
    g_test_add_func("/uuid/unparse_strdup", test_uuid_unparse_strdup);

    return g_test_run();
}
