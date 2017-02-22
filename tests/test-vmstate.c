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


/* Duplicate temp_fd and seek to the beginning of the file */
static QEMUFile *open_test_file(bool write)
{
    int fd = dup(temp_fd);
    QIOChannel *ioc;
    QEMUFile *f;

    lseek(fd, 0, SEEK_SET);
    if (write) {
        g_assert_cmpint(ftruncate(fd, 0), ==, 0);
    }
    ioc = QIO_CHANNEL(qio_channel_file_new_fd(fd));
    if (write) {
        f = qemu_fopen_channel_output(ioc);
    } else {
        f = qemu_fopen_channel_input(ioc);
    }
    object_unref(OBJECT(ioc));
    return f;
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

static void save_buffer(const uint8_t *buf, size_t buf_size)
{
    QEMUFile *fsave = open_test_file(true);
    qemu_put_buffer(fsave, buf, buf_size);
    qemu_fclose(fsave);
}

static void compare_vmstate(const uint8_t *wire, size_t size)
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
                            int version, const uint8_t *wire, size_t size)
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
                        int version, const uint8_t *wire, size_t size)
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
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    save_buffer(buf, sizeof(buf));

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
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 50,             /* e */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    save_buffer(buf, sizeof(buf));

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
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 30,             /* c */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 50,             /* e */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    save_buffer(buf, sizeof(buf));

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
    uint8_t buf[] = {
        0, 0, 0, 10,             /* a */
        0, 0, 0, 20,             /* b */
        0, 0, 0, 0, 0, 0, 0, 40, /* d */
        0, 0, 0, 0, 0, 0, 0, 60, /* f */
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };
    save_buffer(buf, sizeof(buf));

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

typedef struct {
    int32_t i;
} TestStructTriv;

const VMStateDescription vmsd_tst = {
    .name = "test/tst",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(i, TestStructTriv),
        VMSTATE_END_OF_LIST()
    }
};

/* test array migration */

#define AR_SIZE 4

typedef struct {
    TestStructTriv *ar[AR_SIZE];
} TestArrayOfPtrToStuct;

const VMStateDescription vmsd_arps = {
    .name = "test/arps",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(ar, TestArrayOfPtrToStuct,
                AR_SIZE, 0, vmsd_tst, TestStructTriv),
        VMSTATE_END_OF_LIST()
    }
};

static uint8_t wire_arr_ptr_no0[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x03,
    QEMU_VM_EOF
};

static void test_arr_ptr_str_no0_save(void)
{
    TestStructTriv ar[AR_SIZE] = {{.i = 0}, {.i = 1}, {.i = 2}, {.i = 3} };
    TestArrayOfPtrToStuct sample = {.ar = {&ar[0], &ar[1], &ar[2], &ar[3]} };

    save_vmstate(&vmsd_arps, &sample);
    compare_vmstate(wire_arr_ptr_no0, sizeof(wire_arr_ptr_no0));
}

static void test_arr_ptr_str_no0_load(void)
{
    TestStructTriv ar_gt[AR_SIZE] = {{.i = 0}, {.i = 1}, {.i = 2}, {.i = 3} };
    TestStructTriv ar[AR_SIZE] = {};
    TestArrayOfPtrToStuct obj = {.ar = {&ar[0], &ar[1], &ar[2], &ar[3]} };
    int idx;

    save_buffer(wire_arr_ptr_no0, sizeof(wire_arr_ptr_no0));
    SUCCESS(load_vmstate_one(&vmsd_arps, &obj, 1,
                          wire_arr_ptr_no0, sizeof(wire_arr_ptr_no0)));
    for (idx = 0; idx < AR_SIZE; ++idx) {
        /* compare the target array ar with the ground truth array ar_gt */
        g_assert_cmpint(ar_gt[idx].i, ==, ar[idx].i);
    }
}

static uint8_t wire_arr_ptr_0[] = {
    0x00, 0x00, 0x00, 0x00,
    VMS_NULLPTR_MARKER,
    0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x03,
    QEMU_VM_EOF
};

static void test_arr_ptr_str_0_save(void)
{
    TestStructTriv ar[AR_SIZE] = {{.i = 0}, {.i = 1}, {.i = 2}, {.i = 3} };
    TestArrayOfPtrToStuct sample = {.ar = {&ar[0], NULL, &ar[2], &ar[3]} };

    save_vmstate(&vmsd_arps, &sample);
    compare_vmstate(wire_arr_ptr_0, sizeof(wire_arr_ptr_0));
}

static void test_arr_ptr_str_0_load(void)
{
    TestStructTriv ar_gt[AR_SIZE] = {{.i = 0}, {.i = 0}, {.i = 2}, {.i = 3} };
    TestStructTriv ar[AR_SIZE] = {};
    TestArrayOfPtrToStuct obj = {.ar = {&ar[0], NULL, &ar[2], &ar[3]} };
    int idx;

    save_buffer(wire_arr_ptr_0, sizeof(wire_arr_ptr_0));
    SUCCESS(load_vmstate_one(&vmsd_arps, &obj, 1,
                          wire_arr_ptr_0, sizeof(wire_arr_ptr_0)));
    for (idx = 0; idx < AR_SIZE; ++idx) {
        /* compare the target array ar with the ground truth array ar_gt */
        g_assert_cmpint(ar_gt[idx].i, ==, ar[idx].i);
    }
    for (idx = 0; idx < AR_SIZE; ++idx) {
        if (idx == 1) {
            g_assert_cmpint((uintptr_t)(obj.ar[idx]), ==, 0);
        } else {
            g_assert_cmpint((uintptr_t)(obj.ar[idx]), !=, 0);
        }
    }
}

typedef struct TestArrayOfPtrToInt {
    int32_t *ar[AR_SIZE];
} TestArrayOfPtrToInt;

const VMStateDescription vmsd_arpp = {
    .name = "test/arps",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_ARRAY_OF_POINTER(ar, TestArrayOfPtrToInt,
                AR_SIZE, 0, vmstate_info_int32, int32_t*),
        VMSTATE_END_OF_LIST()
    }
};

static void test_arr_ptr_prim_0_save(void)
{
    int32_t ar[AR_SIZE] = {0 , 1, 2, 3};
    TestArrayOfPtrToInt  sample = {.ar = {&ar[0], NULL, &ar[2], &ar[3]} };

    save_vmstate(&vmsd_arpp, &sample);
    compare_vmstate(wire_arr_ptr_0, sizeof(wire_arr_ptr_0));
}

static void test_arr_ptr_prim_0_load(void)
{
    int32_t ar_gt[AR_SIZE] = {0, 1, 2, 3};
    int32_t ar[AR_SIZE] = {3 , 42, 1, 0};
    TestArrayOfPtrToInt obj = {.ar = {&ar[0], NULL, &ar[2], &ar[3]} };
    int idx;

    save_buffer(wire_arr_ptr_0, sizeof(wire_arr_ptr_0));
    SUCCESS(load_vmstate_one(&vmsd_arpp, &obj, 1,
                          wire_arr_ptr_0, sizeof(wire_arr_ptr_0)));
    for (idx = 0; idx < AR_SIZE; ++idx) {
        /* compare the target array ar with the ground truth array ar_gt */
        if (idx == 1) {
            g_assert_cmpint(42, ==, ar[idx]);
        } else {
            g_assert_cmpint(ar_gt[idx], ==, ar[idx]);
        }
    }
}

/* test QTAILQ migration */
typedef struct TestQtailqElement TestQtailqElement;

struct TestQtailqElement {
    bool     b;
    uint8_t  u8;
    QTAILQ_ENTRY(TestQtailqElement) next;
};

typedef struct TestQtailq {
    int16_t  i16;
    QTAILQ_HEAD(TestQtailqHead, TestQtailqElement) q;
    int32_t  i32;
} TestQtailq;

static const VMStateDescription vmstate_q_element = {
    .name = "test/queue-element",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(b, TestQtailqElement),
        VMSTATE_UINT8(u8, TestQtailqElement),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_q = {
    .name = "test/queue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT16(i16, TestQtailq),
        VMSTATE_QTAILQ_V(q, TestQtailq, 1, vmstate_q_element, TestQtailqElement,
                         next),
        VMSTATE_INT32(i32, TestQtailq),
        VMSTATE_END_OF_LIST()
    }
};

uint8_t wire_q[] = {
    /* i16 */                     0xfe, 0x0,
    /* start of element 0 of q */ 0x01,
    /* .b  */                     0x01,
    /* .u8 */                     0x82,
    /* start of element 1 of q */ 0x01,
    /* b */                       0x00,
    /* u8 */                      0x41,
    /* end of q */                0x00,
    /* i32 */                     0x00, 0x01, 0x11, 0x70,
    QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
};

static void test_save_q(void)
{
    TestQtailq obj_q = {
        .i16 = -512,
        .i32 = 70000,
    };

    TestQtailqElement obj_qe1 = {
        .b = true,
        .u8 = 130,
    };

    TestQtailqElement obj_qe2 = {
        .b = false,
        .u8 = 65,
    };

    QTAILQ_INIT(&obj_q.q);
    QTAILQ_INSERT_TAIL(&obj_q.q, &obj_qe1, next);
    QTAILQ_INSERT_TAIL(&obj_q.q, &obj_qe2, next);

    save_vmstate(&vmstate_q, &obj_q);
    compare_vmstate(wire_q, sizeof(wire_q));
}

static void test_load_q(void)
{
    TestQtailq obj_q = {
        .i16 = -512,
        .i32 = 70000,
    };

    TestQtailqElement obj_qe1 = {
        .b = true,
        .u8 = 130,
    };

    TestQtailqElement obj_qe2 = {
        .b = false,
        .u8 = 65,
    };

    QTAILQ_INIT(&obj_q.q);
    QTAILQ_INSERT_TAIL(&obj_q.q, &obj_qe1, next);
    QTAILQ_INSERT_TAIL(&obj_q.q, &obj_qe2, next);

    QEMUFile *fsave = open_test_file(true);

    qemu_put_buffer(fsave, wire_q, sizeof(wire_q));
    g_assert(!qemu_file_get_error(fsave));
    qemu_fclose(fsave);

    QEMUFile *fload = open_test_file(false);
    TestQtailq tgt;

    QTAILQ_INIT(&tgt.q);
    vmstate_load_state(fload, &vmstate_q, &tgt, 1);
    char eof = qemu_get_byte(fload);
    g_assert(!qemu_file_get_error(fload));
    g_assert_cmpint(tgt.i16, ==, obj_q.i16);
    g_assert_cmpint(tgt.i32, ==, obj_q.i32);
    g_assert_cmpint(eof, ==, QEMU_VM_EOF);

    TestQtailqElement *qele_from = QTAILQ_FIRST(&obj_q.q);
    TestQtailqElement *qlast_from = QTAILQ_LAST(&obj_q.q, TestQtailqHead);
    TestQtailqElement *qele_to = QTAILQ_FIRST(&tgt.q);
    TestQtailqElement *qlast_to = QTAILQ_LAST(&tgt.q, TestQtailqHead);

    while (1) {
        g_assert_cmpint(qele_to->b, ==, qele_from->b);
        g_assert_cmpint(qele_to->u8, ==, qele_from->u8);
        if ((qele_from == qlast_from) || (qele_to == qlast_to)) {
            break;
        }
        qele_from = QTAILQ_NEXT(qele_from, next);
        qele_to = QTAILQ_NEXT(qele_to, next);
    }

    g_assert_cmpint((uintptr_t) qele_from, ==, (uintptr_t) qlast_from);
    g_assert_cmpint((uintptr_t) qele_to, ==, (uintptr_t) qlast_to);

    /* clean up */
    TestQtailqElement *qele;
    while (!QTAILQ_EMPTY(&tgt.q)) {
        qele = QTAILQ_LAST(&tgt.q, TestQtailqHead);
        QTAILQ_REMOVE(&tgt.q, qele, next);
        free(qele);
        qele = NULL;
    }
    qemu_fclose(fload);
}

typedef struct TmpTestStruct {
    TestStruct *parent;
    int64_t diff;
} TmpTestStruct;

static void tmp_child_pre_save(void *opaque)
{
    struct TmpTestStruct *tts = opaque;

    tts->diff = tts->parent->b - tts->parent->a;
}

static int tmp_child_post_load(void *opaque, int version_id)
{
    struct TmpTestStruct *tts = opaque;

    tts->parent->b = tts->parent->a + tts->diff;

    return 0;
}

static const VMStateDescription vmstate_tmp_back_to_parent = {
    .name = "test/tmp_child_parent",
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(f, TestStruct),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tmp_child = {
    .name = "test/tmp_child",
    .pre_save = tmp_child_pre_save,
    .post_load = tmp_child_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(diff, TmpTestStruct),
        VMSTATE_STRUCT_POINTER(parent, TmpTestStruct,
                               vmstate_tmp_back_to_parent, TestStruct),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_with_tmp = {
    .name = "test/with_tmp",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(a, TestStruct),
        VMSTATE_UINT64(d, TestStruct),
        VMSTATE_WITH_TMP(TestStruct, TmpTestStruct, vmstate_tmp_child),
        VMSTATE_END_OF_LIST()
    }
};

static void obj_tmp_copy(void *target, void *source)
{
    memcpy(target, source, sizeof(TestStruct));
}

static void test_tmp_struct(void)
{
    TestStruct obj, obj_clone;

    uint8_t const wire_with_tmp[] = {
        /* u32 a */ 0x00, 0x00, 0x00, 0x02,
        /* u64 d */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        /* diff  */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        /* u64 f */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
        QEMU_VM_EOF, /* just to ensure we won't get EOF reported prematurely */
    };

    memset(&obj, 0, sizeof(obj));
    obj.a = 2;
    obj.b = 4;
    obj.d = 1;
    obj.f = 8;
    save_vmstate(&vmstate_with_tmp, &obj);

    compare_vmstate(wire_with_tmp, sizeof(wire_with_tmp));

    memset(&obj, 0, sizeof(obj));
    SUCCESS(load_vmstate(&vmstate_with_tmp, &obj, &obj_clone,
                         obj_tmp_copy, 1, wire_with_tmp,
                         sizeof(wire_with_tmp)));
    g_assert_cmpint(obj.a, ==, 2); /* From top level vmsd */
    g_assert_cmpint(obj.b, ==, 4); /* from the post_load */
    g_assert_cmpint(obj.d, ==, 1); /* From top level vmsd */
    g_assert_cmpint(obj.f, ==, 8); /* From the child->parent */
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
    g_test_add_func("/vmstate/array/ptr/str/no0/save",
                    test_arr_ptr_str_no0_save);
    g_test_add_func("/vmstate/array/ptr/str/no0/load",
                    test_arr_ptr_str_no0_load);
    g_test_add_func("/vmstate/array/ptr/str/0/save", test_arr_ptr_str_0_save);
    g_test_add_func("/vmstate/array/ptr/str/0/load",
                    test_arr_ptr_str_0_load);
    g_test_add_func("/vmstate/array/ptr/prim/0/save",
                    test_arr_ptr_prim_0_save);
    g_test_add_func("/vmstate/array/ptr/prim/0/load",
                    test_arr_ptr_prim_0_load);
    g_test_add_func("/vmstate/qtailq/save/saveq", test_save_q);
    g_test_add_func("/vmstate/qtailq/load/loadq", test_load_q);
    g_test_add_func("/vmstate/tmp_struct", test_tmp_struct);
    g_test_run();

    close(temp_fd);
    unlink(temp_file);

    return 0;
}
