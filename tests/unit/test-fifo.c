/*
 * Fifo8 tests
 *
 * Copyright 2024 Mark Cave-Ayland
 *
 * Authors:
 *  Mark Cave-Ayland    <mark.cave-ayland@ilande.co.uk>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/fifo8.h"

const VMStateInfo vmstate_info_uint32;
const VMStateInfo vmstate_info_buffer;


static void test_fifo8_pop_bufptr_wrap(void)
{
    Fifo8 fifo;
    uint8_t data_in1[] = { 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_in2[] = { 0x5, 0x6, 0x7, 0x8, 0x1, 0x2 };
    const uint8_t *buf;
    uint32_t count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in1, sizeof(data_in1));
    buf = fifo8_pop_bufptr(&fifo, 2, &count);
    g_assert(count == 2);
    g_assert(buf[0] == 0x1 && buf[1] == 0x2);

    fifo8_push_all(&fifo, data_in2, sizeof(data_in2));
    buf = fifo8_pop_bufptr(&fifo, 8, &count);
    g_assert(count == 6);
    g_assert(buf[0] == 0x3 && buf[1] == 0x4 && buf[2] == 0x5 &&
             buf[3] == 0x6 && buf[4] == 0x7 && buf[5] == 0x8);

    g_assert(fifo8_num_used(&fifo) == 2);
    fifo8_destroy(&fifo);
}

static void test_fifo8_pop_bufptr(void)
{
    Fifo8 fifo;
    uint8_t data_in[] = { 0x1, 0x2, 0x3, 0x4 };
    const uint8_t *buf;
    uint32_t count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in, sizeof(data_in));
    buf = fifo8_pop_bufptr(&fifo, 2, &count);
    g_assert(count == 2);
    g_assert(buf[0] == 0x1 && buf[1] == 0x2);

    g_assert(fifo8_num_used(&fifo) == 2);
    fifo8_destroy(&fifo);
}

static void test_fifo8_peek_bufptr_wrap(void)
{
    Fifo8 fifo;
    uint8_t data_in1[] = { 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_in2[] = { 0x5, 0x6, 0x7, 0x8, 0x1, 0x2 };
    const uint8_t *buf;
    uint32_t count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in1, sizeof(data_in1));
    buf = fifo8_peek_bufptr(&fifo, 2, &count);
    g_assert(count == 2);
    g_assert(buf[0] == 0x1 && buf[1] == 0x2);

    buf = fifo8_pop_bufptr(&fifo, 2, &count);
    g_assert(count == 2);
    g_assert(buf[0] == 0x1 && buf[1] == 0x2);
    fifo8_push_all(&fifo, data_in2, sizeof(data_in2));

    buf = fifo8_peek_bufptr(&fifo, 8, &count);
    g_assert(count == 6);
    g_assert(buf[0] == 0x3 && buf[1] == 0x4 && buf[2] == 0x5 &&
             buf[3] == 0x6 && buf[4] == 0x7 && buf[5] == 0x8);

    g_assert(fifo8_num_used(&fifo) == 8);
    fifo8_destroy(&fifo);
}

static void test_fifo8_peek_bufptr(void)
{
    Fifo8 fifo;
    uint8_t data_in[] = { 0x1, 0x2, 0x3, 0x4 };
    const uint8_t *buf;
    uint32_t count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in, sizeof(data_in));
    buf = fifo8_peek_bufptr(&fifo, 2, &count);
    g_assert(count == 2);
    g_assert(buf[0] == 0x1 && buf[1] == 0x2);

    g_assert(fifo8_num_used(&fifo) == 4);
    fifo8_destroy(&fifo);
}

static void test_fifo8_pop_buf_wrap(void)
{
    Fifo8 fifo;
    uint8_t data_in1[] = { 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_in2[] = { 0x5, 0x6, 0x7, 0x8, 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_out[4];
    int count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in1, sizeof(data_in1));
    fifo8_pop_buf(&fifo, NULL, 4);

    fifo8_push_all(&fifo, data_in2, sizeof(data_in2));
    count = fifo8_pop_buf(&fifo, NULL, 4);
    g_assert(count == 4);
    count = fifo8_pop_buf(&fifo, data_out, 4);
    g_assert(count == 4);
    g_assert(data_out[0] == 0x1 && data_out[1] == 0x2 &&
             data_out[2] == 0x3 && data_out[3] == 0x4);

    g_assert(fifo8_num_used(&fifo) == 0);
    fifo8_destroy(&fifo);
}

static void test_fifo8_pop_buf(void)
{
    Fifo8 fifo;
    uint8_t data_in[] = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8 };
    uint8_t data_out[] = { 0xff, 0xff, 0xff, 0xff };
    int count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in, sizeof(data_in));
    count = fifo8_pop_buf(&fifo, NULL, 4);
    g_assert(count == 4);
    count = fifo8_pop_buf(&fifo, data_out, 4);
    g_assert(data_out[0] == 0x5 && data_out[1] == 0x6 &&
             data_out[2] == 0x7 && data_out[3] == 0x8);

    g_assert(fifo8_num_used(&fifo) == 0);
    fifo8_destroy(&fifo);
}

static void test_fifo8_peek_buf_wrap(void)
{
    Fifo8 fifo;
    uint8_t data_in1[] = { 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_in2[] = { 0x5, 0x6, 0x7, 0x8, 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_out[4];
    int count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in1, sizeof(data_in1));
    fifo8_pop_buf(&fifo, NULL, 4);

    fifo8_push_all(&fifo, data_in2, sizeof(data_in2));
    count = fifo8_peek_buf(&fifo, NULL, 4);
    g_assert(count == 4);
    count = fifo8_peek_buf(&fifo, data_out, 4);
    g_assert(count == 4);
    g_assert(data_out[0] == 0x5 && data_out[1] == 0x6 &&
             data_out[2] == 0x7 && data_out[3] == 0x8);

    g_assert(fifo8_num_used(&fifo) == 8);
    fifo8_destroy(&fifo);
}

static void test_fifo8_peek_buf(void)
{
    Fifo8 fifo;
    uint8_t data_in[] = { 0x1, 0x2, 0x3, 0x4 };
    uint8_t data_out[] = { 0xff, 0xff, 0xff, 0xff };
    int count;

    fifo8_create(&fifo, 8);

    fifo8_push_all(&fifo, data_in, sizeof(data_in));
    count = fifo8_peek_buf(&fifo, NULL, 4);
    g_assert(count == 4);
    g_assert(data_out[0] == 0xff && data_out[1] == 0xff &&
             data_out[2] == 0xff && data_out[3] == 0xff);

    count = fifo8_peek_buf(&fifo, data_out, 4);
    g_assert(count == 4);
    g_assert(data_out[0] == 0x1 && data_out[1] == 0x2 &&
             data_out[2] == 0x3 && data_out[3] == 0x4);

    g_assert(fifo8_num_used(&fifo) == 4);
    fifo8_destroy(&fifo);
}

static void test_fifo8_peek(void)
{
    Fifo8 fifo;
    uint8_t c;

    fifo8_create(&fifo, 8);
    fifo8_push(&fifo, 0x1);
    fifo8_push(&fifo, 0x2);

    c = fifo8_peek(&fifo);
    g_assert(c == 0x1);
    fifo8_pop(&fifo);
    c = fifo8_peek(&fifo);
    g_assert(c == 0x2);

    g_assert(fifo8_num_used(&fifo) == 1);
    fifo8_destroy(&fifo);
}

static void test_fifo8_pushpop(void)
{
    Fifo8 fifo;
    uint8_t c;

    fifo8_create(&fifo, 8);
    fifo8_push(&fifo, 0x1);
    fifo8_push(&fifo, 0x2);

    c = fifo8_pop(&fifo);
    g_assert(c == 0x1);
    c = fifo8_pop(&fifo);
    g_assert(c == 0x2);

    g_assert(fifo8_num_used(&fifo) == 0);
    fifo8_destroy(&fifo);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/fifo8/pushpop", test_fifo8_pushpop);
    g_test_add_func("/fifo8/peek", test_fifo8_peek);
    g_test_add_func("/fifo8/peek_buf", test_fifo8_peek_buf);
    g_test_add_func("/fifo8/peek_buf_wrap", test_fifo8_peek_buf_wrap);
    g_test_add_func("/fifo8/pop_buf", test_fifo8_pop_buf);
    g_test_add_func("/fifo8/pop_buf_wrap", test_fifo8_pop_buf_wrap);
    g_test_add_func("/fifo8/peek_bufptr", test_fifo8_peek_bufptr);
    g_test_add_func("/fifo8/peek_bufptr_wrap", test_fifo8_peek_bufptr_wrap);
    g_test_add_func("/fifo8/pop_bufptr", test_fifo8_pop_bufptr);
    g_test_add_func("/fifo8/pop_bufptr_wrap", test_fifo8_pop_bufptr_wrap);
    return g_test_run();
}
