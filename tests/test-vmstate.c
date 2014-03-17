/*
 *  Test code for VMState
 *
 *  Copyright (c) 2013 Red Hat Inc.
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

#include <glib.h>

#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "block/coroutine.h"

char temp_file[] = "/tmp/vmst.test.XXXXXX";
int temp_fd;

/* Fake yield_until_fd_readable() implementation so we don't have to pull the
 * coroutine code as dependency.
 */
void yield_until_fd_readable(int fd)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    select(fd + 1, &fds, NULL, NULL, NULL);
}

/* Duplicate temp_fd and seek to the beginning of the file */
static int dup_temp_fd(bool truncate)
{
    int fd = dup(temp_fd);
    lseek(fd, 0, SEEK_SET);
    if (truncate) {
        g_assert_cmpint(ftruncate(fd, 0), ==, 0);
    }
    return fd;
}

typedef struct TestSruct {
    uint32_t a, b, c, e;
    uint64_t d, f;
    bool skip_c_e;
} TestStruct;


static const VMStateDescription vmstate_simple = {
    .name = "test",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(a, TestStruct),
        VMSTATE_UINT32(b, TestStruct),
        VMSTATE_UINT32(c, TestStruct),
        VMSTATE_UINT64(d, TestStruct),
        VMSTATE_END_OF_LIST()
    }
};

static void test_simple_save(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    TestStruct obj = { .a = 1, .b = 2, .c = 3, .d = 4 };
    vmstate_save_state(fsave, &vmstate_simple, &obj);
    g_assert(!qemu_file_get_error(fsave));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    uint8_t expected[] = {
        0, 0, 0, 1, /* a */
        0, 0, 0, 2, /* b */
        0, 0, 0, 3, /* c */
        0, 0, 0, 0, 0, 0, 0, 4, /* d */
    };
    uint8_t result[sizeof(expected)];
    g_assert_cmpint(qemu_get_buffer(loading, result, sizeof(result)), ==,
                    sizeof(result));
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(memcmp(result, expected, sizeof(result)), ==, 0);

    /* Must reach EOF */
    qemu_get_byte(loading);
    g_assert_cmpint(qemu_file_get_error(loading), ==, -EIO);

    qemu_fclose(loading);
}

static void test_simple_load(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    TestStruct obj;
    vmstate_load_state(loading, &vmstate_simple, &obj, 1);
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(obj.a, ==, 10);
    g_assert_cmpint(obj.b, ==, 20);
    g_assert_cmpint(obj.c, ==, 30);
    g_assert_cmpint(obj.d, ==, 40);
    qemu_fclose(loading);
}

static const VMStateDescription vmstate_versioned = {
    .name = "test",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(a, TestStruct),
        VMSTATE_UINT32_V(b, TestStruct, 2), /* Versioned field in the middle, so
                                             * we catch bugs more easily.
                                             */
        VMSTATE_UINT32(c, TestStruct),
        VMSTATE_UINT64(d, TestStruct),
        VMSTATE_UINT32_V(e, TestStruct, 2),
        VMSTATE_UINT64_V(f, TestStruct, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void test_load_v1(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    TestStruct obj = { .b = 200, .e = 500, .f = 600 };
    vmstate_load_state(loading, &vmstate_versioned, &obj, 1);
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(obj.a, ==, 10);
    g_assert_cmpint(obj.b, ==, 200);
    g_assert_cmpint(obj.c, ==, 30);
    g_assert_cmpint(obj.d, ==, 40);
    g_assert_cmpint(obj.e, ==, 500);
    g_assert_cmpint(obj.f, ==, 600);
    qemu_fclose(loading);
}

static void test_load_v2(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 50,             /* e */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    TestStruct obj;
    vmstate_load_state(loading, &vmstate_versioned, &obj, 2);
    g_assert_cmpint(obj.a, ==, 10);
    g_assert_cmpint(obj.b, ==, 20);
    g_assert_cmpint(obj.c, ==, 30);
    g_assert_cmpint(obj.d, ==, 40);
    g_assert_cmpint(obj.e, ==, 50);
    g_assert_cmpint(obj.f, ==, 60);
    qemu_fclose(loading);
}

static bool test_skip(void *opaque, int version_id)
{
    TestStruct *t = (TestStruct *)opaque;
    return !t->skip_c_e;
}

static const VMStateDescription vmstate_skipping = {
    .name = "test",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(a, TestStruct),
        VMSTATE_UINT32(b, TestStruct),
        VMSTATE_UINT32_TEST(c, TestStruct, test_skip),
        VMSTATE_UINT64(d, TestStruct),
        VMSTATE_UINT32_TEST(e, TestStruct, test_skip),
        VMSTATE_UINT64_V(f, TestStruct, 2),
        VMSTATE_END_OF_LIST()
    }
};


static void test_save_noskip(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    TestStruct obj = { .a = 1, .b = 2, .c = 3, .d = 4, .e = 5, .f = 6,
                       .skip_c_e = false };
    vmstate_save_state(fsave, &vmstate_skipping, &obj);
    g_assert(!qemu_file_get_error(fsave));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    uint8_t expected[] = {
        0, 0, 0, 1,             /* a */
        0, 0, 0, 2,             /* b */
        0, 0, 0, 3,             /* c */
        0, 0, 0, 0, 0, 0, 0, 4, /* d */
        0, 0, 0, 5,             /* e */
        0, 0, 0, 0, 0, 0, 0, 6, /* f */
    };
    uint8_t result[sizeof(expected)];
    g_assert_cmpint(qemu_get_buffer(loading, result, sizeof(result)), ==,
                    sizeof(result));
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(memcmp(result, expected, sizeof(result)), ==, 0);

    /* Must reach EOF */
    qemu_get_byte(loading);
    g_assert_cmpint(qemu_file_get_error(loading), ==, -EIO);

    qemu_fclose(loading);
}

static void test_save_skip(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    TestStruct obj = { .a = 1, .b = 2, .c = 3, .d = 4, .e = 5, .f = 6,
                       .skip_c_e = true };
    vmstate_save_state(fsave, &vmstate_skipping, &obj);
    g_assert(!qemu_file_get_error(fsave));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    uint8_t expected[] = {
        0, 0, 0, 1,             /* a */
        0, 0, 0, 2,             /* b */
        0, 0, 0, 0, 0, 0, 0, 4, /* d */
        0, 0, 0, 0, 0, 0, 0, 6, /* f */
    };
    uint8_t result[sizeof(expected)];
    g_assert_cmpint(qemu_get_buffer(loading, result, sizeof(result)), ==,
                    sizeof(result));
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(memcmp(result, expected, sizeof(result)), ==, 0);


    /* Must reach EOF */
    qemu_get_byte(loading);
    g_assert_cmpint(qemu_file_get_error(loading), ==, -EIO);

    qemu_fclose(loading);
}

static void test_load_noskip(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 50,             /* e */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    TestStruct obj = { .skip_c_e = false };
    vmstate_load_state(loading, &vmstate_skipping, &obj, 2);
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(obj.a, ==, 10);
    g_assert_cmpint(obj.b, ==, 20);
    g_assert_cmpint(obj.c, ==, 30);
    g_assert_cmpint(obj.d, ==, 40);
    g_assert_cmpint(obj.e, ==, 50);
    g_assert_cmpint(obj.f, ==, 60);
    qemu_fclose(loading);
}

static void test_load_skip(void)
{
    QEMUFile *fsave = qemu_fdopen(dup_temp_fd(true), "wb");
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = qemu_fdopen(dup_temp_fd(false), "rb");
    TestStruct obj = { .skip_c_e = true, .c = 300, .e = 500 };
    vmstate_load_state(loading, &vmstate_skipping, &obj, 2);
    g_assert(!qemu_file_get_error(loading));
    g_assert_cmpint(obj.a, ==, 10);
    g_assert_cmpint(obj.b, ==, 20);
    g_assert_cmpint(obj.c, ==, 300);
    g_assert_cmpint(obj.d, ==, 40);
    g_assert_cmpint(obj.e, ==, 500);
    g_assert_cmpint(obj.f, ==, 60);
    qemu_fclose(loading);
}

int main(int argc, char **argv)
{
    temp_fd = mkstemp(temp_file);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vmstate/simple/save", test_simple_save);
    g_test_add_func("/vmstate/simple/load", test_simple_load);
    g_test_add_func("/vmstate/versioned/load/v1", test_load_v1);
    g_test_add_func("/vmstate/versioned/load/v2", test_load_v2);
    g_test_add_func("/vmstate/field_exists/load/noskip", test_load_noskip);
    g_test_add_func("/vmstate/field_exists/load/skip", test_load_skip);
    g_test_add_func("/vmstate/field_exists/save/noskip", test_save_noskip);
    g_test_add_func("/vmstate/field_exists/save/skip", test_save_skip);
    g_test_run();

    close(temp_fd);
    unlink(temp_file);

    return 0;
}
