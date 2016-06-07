/*
 * QEMU base64 helper test
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include "qapi/error.h"
#include "qemu/base64.h"

static void test_base64_good(void)
{
    const char input[] =
        "QmVjYXVzZSB3ZSBmb2N1c2VkIG9uIHRoZSBzbmFrZSwgd2UgbW\n"
        "lzc2VkIHRoZSBzY29ycGlvbi4=";
    const char expect[] = "Because we focused on the snake, "
        "we missed the scorpion.";

    size_t len;
    uint8_t *actual = qbase64_decode(input,
                                     -1,
                                     &len,
                                     &error_abort);

    g_assert(actual != NULL);
    g_assert_cmpint(len, ==, strlen(expect));
    g_assert_cmpstr((char *)actual, ==, expect);
    g_free(actual);
}


static void test_base64_bad(const char *input,
                            size_t input_len)
{
    size_t len;
    Error *err = NULL;
    uint8_t *actual = qbase64_decode(input,
                                     input_len,
                                     &len,
                                     &err);

    g_assert(err != NULL);
    g_assert(actual == NULL);
    g_assert_cmpint(len, ==, 0);
    error_free(err);
}


static void test_base64_embedded_nul(void)
{
    /* We put a NUL character in the middle of the base64
     * text which is invalid data, given the expected length */
    const char input[] =
        "QmVjYXVzZSB3ZSBmb2N1c2VkIG9uIHRoZSBzbmFrZSwgd2UgbW\0"
        "lzc2VkIHRoZSBzY29ycGlvbi4=";

    test_base64_bad(input, G_N_ELEMENTS(input) - 1);
}


static void test_base64_not_nul_terminated(void)
{
    const char input[] =
        "QmVjYXVzZSB3ZSBmb2N1c2VkIG9uIHRoZSBzbmFrZSwgd2UgbW\n"
        "lzc2VkIHRoZSBzY29ycGlvbi4=";

    /* Using '-2' to make us drop the trailing NUL, thus
     * creating an invalid base64 sequence for decoding */
    test_base64_bad(input, G_N_ELEMENTS(input) - 2);
}


static void test_base64_invalid_chars(void)
{
    /* We put a single quote character in the middle
     * of the base64 text which is invalid data */
    const char input[] =
        "QmVjYXVzZSB3ZSBmb2N1c2VkIG9uIHRoZSBzbmFrZSwgd2UgbW'"
        "lzc2VkIHRoZSBzY29ycGlvbi4=";

    test_base64_bad(input, strlen(input));
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/util/base64/good", test_base64_good);
    g_test_add_func("/util/base64/embedded-nul", test_base64_embedded_nul);
    g_test_add_func("/util/base64/not-nul-terminated",
                    test_base64_not_nul_terminated);
    g_test_add_func("/util/base64/invalid-chars", test_base64_invalid_chars);
    return g_test_run();
}
