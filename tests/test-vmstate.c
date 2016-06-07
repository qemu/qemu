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

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "qemu/coroutine.h"
#include "io/channel-file.h"

static char temp_file[] = "/tmp/vmst.test.XXXXXX";
static int temp_fd;

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
static QEMUFile *open_test_file(bool write)
{
    int fd = dup(temp_fd);
    QIOChannel *ioc;
    lseek(fd, 0, SEEK_SET);
    if (write) {
        g_assert_cmpint(ftruncate(fd, 0), ==, 0);
    }
    ioc = QIO_CHANNEL(qio_channel_file_new_fd(fd));
    if (write) {
        return qemu_fopen_channel_output(ioc);
    } else {
        return qemu_fopen_channel_input(ioc);
    }
}

#define SUCCESS(val) \
    g_assert_cmpint((val), ==, 0)

#define FAILURE(val) \
    g_assert_cmpint((val), !=, 0)

static void save_vmstate(const VMStateDescription *desc, void *obj)
{
    QEMUFile *f = open_test_file(true);

    /* Save file with vmstate */
    vmstate_save_state(f, desc, obj, NULL);
    qemu_put_byte(f, QEMU_VM_EOF);
    g_assert(!qemu_file_get_error(f));
    qemu_fclose(f);
}

static void compare_vmstate(uint8_t *wire, size_t size)
{
    QEMUFile *f = open_test_file(false);
    uint8_t result[size];

    /* read back as binary */

    g_assert_cmpint(qemu_get_buffer(f, result, sizeof(result)), ==,
                    sizeof(result));
    g_assert(!qemu_file_get_error(f));

    /* Compare that what is on the file is the same that what we
       expected to be there */
    SUCCESS(memcmp(result, wire, sizeof(result)));

    /* Must reach EOF */
    qemu_get_byte(f);
    g_assert_cmpint(qemu_file_get_error(f), ==, -EIO);

    qemu_fclose(f);
}

static int load_vmstate_one(const VMStateDescription *desc, void *obj,
                            int version, uint8_t *wire, size_t size)
{
    QEMUFile *f;
    int ret;

    f = open_test_file(true);
    qemu_put_buffer(f, wire, size);
    qemu_fclose(f);

    f = open_test_file(false);
    ret = vmstate_load_state(f, desc, obj, version);
    if (ret) {
        g_assert(qemu_file_get_error(f));
    } else{
        g_assert(!qemu_file_get_error(f));
    }
    qemu_fclose(f);
    return ret;
}


static int load_vmstate(const VMStateDescription *desc,
                        void *obj, void *obj_clone,
                        void (*obj_copy)(void *, void*),
                        int version, uint8_t *wire, size_t size)
{
    /* We test with zero size */
    obj_copy(obj_clone, obj);
    FAILURE(load_vmstate_one(desc, obj, version, wire, 0));

    /* Stream ends with QEMU_EOF, so we need at least 3 bytes to be
     * able to test in the middle */

    if (size > 3) {

        /* We test with size - 2. We can't test size - 1 due to EOF tricks */
        obj_copy(obj, obj_clone);
        FAILURE(load_vmstate_one(desc, obj, version, wire, size - 2));

        /* Test with size/2, first half of real state */
        obj_copy(obj, obj_clone);
        FAILURE(load_vmstate_one(desc, obj, version, wire, size/2));

        /* Test with size/2, second half of real state */
        obj_copy(obj, obj_clone);
        FAILURE(load_vmstate_one(desc, obj, version, wire + (size/2), size/2));

    }
    obj_copy(obj, obj_clone);
    return load_vmstate_one(desc, obj, version, wire, size);
}

/* Test struct that we are going to use for our tests */

typedef struct TestSimple {
    bool     b_1,   b_2;
    uint8_t  u8_1;
    uint16_t u16_1;
    uint32_t u32_1;
    uint64_t u64_1;
    int8_t   i8_1,  i8_2;
    int16_t  i16_1, i16_2;
    int32_t  i32_1, i32_2;
    int64_t  i64_1, i64_2;
} TestSimple;

/* Object instantiation, we are going to use it in more than one test */

TestSimple obj_simple = {
    .b_1 = true,
    .b_2 = false,
    .u8_1 = 130,
    .u16_1 = 512,
    .u32_1 = 70000,
    .u64_1 = 12121212,
    .i8_1 = 65,
    .i8_2 = -65,
    .i16_1 = 512,
    .i16_2 = -512,
    .i32_1 = 70000,
    .i32_2 = -70000,
    .i64_1 = 12121212,
    .i64_2 = -12121212,
};

/* Description of the values.  If you add a primitive type
   you are expected to add a test here */

static const VMStateDescription vmstate_simple_primitive = {
    .name = "simple/primitive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(b_1, TestSimple),
        VMSTATE_BOOL(b_2, TestSimple),
        VMSTATE_UINT8(u8_1, TestSimple),
        VMSTATE_UINT16(u16_1, TestSimple),
        VMSTATE_UINT32(u32_1, TestSimple),
        VMSTATE_UINT64(u64_1, TestSimple),
        VMSTATE_INT8(i8_1, TestSimple),
        VMSTATE_INT8(i8_2, TestSimple),
        VMSTATE_INT16(i16_1, TestSimple),
        VMSTATE_INT16(i16_2, TestSimple),
        VMSTATE_INT32(i32_1, TestSimple),
        VMSTATE_INT32(i32_2, TestSimple),
        VMSTATE_INT64(i64_1, TestSimple),
        VMSTATE_INT64(i64_2, TestSimple),
        VMSTATE_END_OF_LIST()
    }
};

/* It describes what goes through the wire.  Our tests are basically:

   * save test
     - save a struct a vmstate to a file
     - read that file back (binary read, no vmstate)
     - compare it with what we expect to be on the wire
   * load test
     - save to the file what we expect to be on the wire
     - read struct back with vmstate in a different
     - compare back with the original struct
*/

uint8_t wire_simple_primitive[] = {
    /* b_1 */   0x01,
    /* b_2 */   0x00,
    /* u8_1 */  0x82,
    /* u16_1 */ 0x02, 0x00,
    /* u32_1 */ 0x00, 0x01, 0x11, 0x70,
    /* u64_1 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0xf4, 0x7c,
    /* i8_1 */  0x41,
    /* i8_2 */  0xbf,
    /* i16_1 */ 0x02, 0x00,
    /* i16_2 */ 0xfe, 0x0,
    /* i32_1 */ 0x00, 0x01, 0x11, 0x70,
    /* i32_2 */ 0xff, 0xfe, 0xee, 0x90,
    /* i64_1 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0xf4, 0x7c,
    /* i64_2 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0x47, 0x0b, 0x84,
    QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
};

static void obj_simple_copy(void *target, void *source)
{
    memcpy(target, source, sizeof(TestSimple));
}

static void test_simple_primitive(void)
{
    TestSimple obj, obj_clone;

    memset(&obj, 0, sizeof(obj));
    save_vmstate(&vmstate_simple_primitive, &obj_simple);

    compare_vmstate(wire_simple_primitive, sizeof(wire_simple_primitive));

    SUCCESS(load_vmstate(&vmstate_simple_primitive, &obj, &obj_clone,
                         obj_simple_copy, 1, wire_simple_primitive,
                         sizeof(wire_simple_primitive)));

#define FIELD_EQUAL(name)   g_assert_cmpint(obj.name, ==, obj_simple.name)

    FIELD_EQUAL(b_1);
    FIELD_EQUAL(b_2);
    FIELD_EQUAL(u8_1);
    FIELD_EQUAL(u16_1);
    FIELD_EQUAL(u32_1);
    FIELD_EQUAL(u64_1);
    FIELD_EQUAL(i8_1);
    FIELD_EQUAL(i8_2);
    FIELD_EQUAL(i16_1);
    FIELD_EQUAL(i16_2);
    FIELD_EQUAL(i32_1);
    FIELD_EQUAL(i32_2);
    FIELD_EQUAL(i64_1);
    FIELD_EQUAL(i64_2);
}
#undef FIELD_EQUAL

typedef struct TestStruct {
    uint32_t a, b, c, e;
    uint64_t d, f;
    bool skip_c_e;
} TestStruct;

static const VMStateDescription vmstate_versioned = {
    .name = "test/versioned",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
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
    QEMUFile *fsave = open_test_file(true);
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = open_test_file(false);
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
    QEMUFile *fsave = open_test_file(true);
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

    QEMUFile *loading = open_test_file(false);
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
    .name = "test/skip",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
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
    QEMUFile *fsave = open_test_file(true);
    TestStruct obj = { .a = 1, .b = 2, .c = 3, .d = 4, .e = 5, .f = 6,
                       .skip_c_e = false };
    vmstate_save_state(fsave, &vmstate_skipping, &obj, NULL);
    g_assert(!qemu_file_get_error(fsave));

    uint8_t expected[] = {
        0, 0, 0, 1,             /* a */
        0, 0, 0, 2,             /* b */
        0, 0, 0, 3,             /* c */
        0, 0, 0, 0, 0, 0, 0, 4, /* d */
        0, 0, 0, 5,             /* e */
        0, 0, 0, 0, 0, 0, 0, 6, /* f */
    };

    qemu_fclose(fsave);
    compare_vmstate(expected, sizeof(expected));
}

static void test_save_skip(void)
{
    QEMUFile *fsave = open_test_file(true);
    TestStruct obj = { .a = 1, .b = 2, .c = 3, .d = 4, .e = 5, .f = 6,
                       .skip_c_e = true };
    vmstate_save_state(fsave, &vmstate_skipping, &obj, NULL);
    g_assert(!qemu_file_get_error(fsave));

    uint8_t expected[] = {
        0, 0, 0, 1,             /* a */
        0, 0, 0, 2,             /* b */
        0, 0, 0, 0, 0, 0, 0, 4, /* d */
        0, 0, 0, 0, 0, 0, 0, 6, /* f */
    };

    qemu_fclose(fsave);
    compare_vmstate(expected, sizeof(expected));
}

static void test_load_noskip(void)
{
    QEMUFile *fsave = open_test_file(true);
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

    QEMUFile *loading = open_test_file(false);
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
    QEMUFile *fsave = open_test_file(true);
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    qemu_put_buffer(fsave, buf, sizeof(buf));
    qemu_fclose(fsave);

    QEMUFile *loading = open_test_file(false);
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

    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vmstate/simple/primitive", test_simple_primitive);
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
