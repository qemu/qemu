/*
 * QEMU Char Device for testsuite control
 *
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
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
#include "qemu-common.h"
#include "sysemu/char.h"

#define BUF_SIZE 32

typedef struct {
    CharDriverState *chr;
    uint8_t in_buf[32];
    int in_buf_used;
} TestdevCharState;

/* Try to interpret a whole incoming packet */
static int testdev_eat_packet(TestdevCharState *testdev)
{
    const uint8_t *cur = testdev->in_buf;
    int len = testdev->in_buf_used;
    uint8_t c;
    int arg;

#define EAT(c) do { \
    if (!len--) {   \
        return 0;   \
    }               \
    c = *cur++;     \
} while (0)

    EAT(c);

    while (isspace(c)) {
        EAT(c);
    }

    arg = 0;
    while (isdigit(c)) {
        arg = arg * 10 + c - '0';
        EAT(c);
    }

    while (isspace(c)) {
        EAT(c);
    }

    switch (c) {
    case 'q':
        exit((arg << 1) | 1);
        break;
    default:
        break;
    }
    return cur - testdev->in_buf;
}

/* The other end is writing some data.  Store it and try to interpret */
static int testdev_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    TestdevCharState *testdev = chr->opaque;
    int tocopy, eaten, orig_len = len;

    while (len) {
        /* Complete our buffer as much as possible */
        tocopy = MIN(len, BUF_SIZE - testdev->in_buf_used);

        memcpy(testdev->in_buf + testdev->in_buf_used, buf, tocopy);
        testdev->in_buf_used += tocopy;
        buf += tocopy;
        len -= tocopy;

        /* Interpret it as much as possible */
        while (testdev->in_buf_used > 0 &&
               (eaten = testdev_eat_packet(testdev)) > 0) {
            memmove(testdev->in_buf, testdev->in_buf + eaten,
                    testdev->in_buf_used - eaten);
            testdev->in_buf_used -= eaten;
        }
    }
    return orig_len;
}

static void testdev_close(struct CharDriverState *chr)
{
    TestdevCharState *testdev = chr->opaque;

    g_free(testdev);
}

CharDriverState *chr_testdev_init(void)
{
    TestdevCharState *testdev;
    CharDriverState *chr;

    testdev = g_malloc0(sizeof(TestdevCharState));
    testdev->chr = chr = g_malloc0(sizeof(CharDriverState));

    chr->opaque = testdev;
    chr->chr_write = testdev_write;
    chr->chr_close = testdev_close;

    return chr;
}

static void register_types(void)
{
    register_char_driver("testdev", CHARDEV_BACKEND_KIND_TESTDEV, NULL);
}

type_init(register_types);
