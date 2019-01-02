/*
 * Copyright IBM, Corp. 2009
 * Copyright (c) 2013, 2015 Red Hat Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlit.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/unicode.h"
#include "qemu-common.h"

static QString *from_json_str(const char *jstr, bool single, Error **errp)
{
    char quote = single ? '\'' : '"';
    char *qjstr = g_strdup_printf("%c%s%c", quote, jstr, quote);
    QString *ret = qobject_to(QString, qobject_from_json(qjstr, errp));

    g_free(qjstr);
    return ret;
}

static char *to_json_str(QString *str)
{
    QString *json = qobject_to_json(QOBJECT(str));
    char *jstr;

    if (!json) {
        return NULL;
    }
    /* peel off double quotes */
    jstr = g_strndup(qstring_get_str(json) + 1,
                     qstring_get_length(json) - 2);
    qobject_unref(json);
    return jstr;
}

static void escaped_string(void)
{
    struct {
        /* Content of JSON string to parse with qobject_from_json() */
        const char *json_in;
        /* Expected parse output; to unparse with qobject_to_json() */
        const char *utf8_out;
        int skip;
    } test_cases[] = {
        { "\\b\\f\\n\\r\\t\\\\\\\"", "\b\f\n\r\t\\\"" },
        { "\\/\\'", "/'", .skip = 1 },
        { "single byte utf-8 \\u0020", "single byte utf-8  ", .skip = 1 },
        { "double byte utf-8 \\u00A2", "double byte utf-8 \xc2\xa2" },
        { "triple byte utf-8 \\u20AC", "triple byte utf-8 \xe2\x82\xac" },
        { "quadruple byte utf-8 \\uD834\\uDD1E", /* U+1D11E */
          "quadruple byte utf-8 \xF0\x9D\x84\x9E" },
        { "\\", NULL },
        { "\\z", NULL },
        { "\\ux", NULL },
        { "\\u1x", NULL },
        { "\\u12x", NULL },
        { "\\u123x", NULL },
        { "\\u12345", "\341\210\2645" },
        { "\\u0000x", "\xC0\x80x" },
        { "unpaired leading surrogate \\uD800", NULL },
        { "unpaired leading surrogate \\uD800\\uCAFE", NULL },
        { "unpaired leading surrogate \\uD800\\uD801\\uDC02", NULL },
        { "unpaired trailing surrogate \\uDC00", NULL },
        { "backward surrogate pair \\uDC00\\uD800", NULL },
        { "noncharacter U+FDD0 \\uFDD0", NULL },
        { "noncharacter U+FDEF \\uFDEF", NULL },
        { "noncharacter U+1FFFE \\uD87F\\uDFFE", NULL },
        { "noncharacter U+10FFFF \\uDC3F\\uDFFF", NULL },
        {}
    };
    int i, j;
    QString *cstr;
    char *jstr;

    for (i = 0; test_cases[i].json_in; i++) {
        for (j = 0; j < 2; j++) {
            if (test_cases[i].utf8_out) {
                cstr = from_json_str(test_cases[i].json_in, j, &error_abort);
                g_assert_cmpstr(qstring_get_try_str(cstr),
                                ==, test_cases[i].utf8_out);
                if (!test_cases[i].skip) {
                    jstr = to_json_str(cstr);
                    g_assert_cmpstr(jstr, ==, test_cases[i].json_in);
                    g_free(jstr);
                }
                qobject_unref(cstr);
            } else {
                cstr = from_json_str(test_cases[i].json_in, j, NULL);
                g_assert(!cstr);
            }
        }
    }
}

static void string_with_quotes(void)
{
    const char *test_cases[] = {
        "\"the bee's knees\"",
        "'double quote \"'",
        NULL
    };
    int i;
    QString *str;
    char *cstr;

    for (i = 0; test_cases[i]; i++) {
        str = qobject_to(QString,
                         qobject_from_json(test_cases[i], &error_abort));
        g_assert(str);
        cstr = g_strndup(test_cases[i] + 1, strlen(test_cases[i]) - 2);
        g_assert_cmpstr(qstring_get_str(str), ==, cstr);
        g_free(cstr);
        qobject_unref(str);
    }
}

static void utf8_string(void)
{
    /*
     * Most test cases are scraped from Markus Kuhn's UTF-8 decoder
     * capability and stress test at
     * http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt
     */
    static const struct {
        /* Content of JSON string to parse with qobject_from_json() */
        const char *json_in;
        /* Expected parse output */
        const char *utf8_out;
        /* Expected unparse output, defaults to @json_in */
        const char *json_out;
    } test_cases[] = {
        /* 0  Control characters */
        {
            /*
             * Note: \x00 is impossible, other representations of
             * U+0000 are covered under 4.3
             */
            "\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
            "\x10\x11\x12\x13\x14\x15\x16\x17"
            "\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F",
            NULL,
            "\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
            "\\b\\t\\n\\u000B\\f\\r\\u000E\\u000F"
            "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
            "\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F",
        },
        /* 1  Some correct UTF-8 text */
        {
            /* a bit of German */
            "Falsches \xC3\x9C" "ben von Xylophonmusik qu\xC3\xA4lt"
            " jeden gr\xC3\xB6\xC3\x9F" "eren Zwerg.",
            "Falsches \xC3\x9C" "ben von Xylophonmusik qu\xC3\xA4lt"
            " jeden gr\xC3\xB6\xC3\x9F" "eren Zwerg.",
            "Falsches \\u00DCben von Xylophonmusik qu\\u00E4lt"
            " jeden gr\\u00F6\\u00DFeren Zwerg.",
        },
        {
            /* a bit of Greek */
            "\xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5",
            "\xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5",
            "\\u03BA\\u1F79\\u03C3\\u03BC\\u03B5",
        },
            /* '%' character when not interpolating */
        {
            "100%",
            "100%",
        },
        /* 2  Boundary condition test cases */
        /* 2.1  First possible sequence of a certain length */
        /*
         * 2.1.1 1 byte U+0020
         * Control characters are already covered by their own test
         * case under 0.  Test the first 1 byte non-control character
         * here.
         */
        {
            " ",
            " ",
        },
        /* 2.1.2  2 bytes U+0080 */
        {
            "\xC2\x80",
            "\xC2\x80",
            "\\u0080",
        },
        /* 2.1.3  3 bytes U+0800 */
        {
            "\xE0\xA0\x80",
            "\xE0\xA0\x80",
            "\\u0800",
        },
        /* 2.1.4  4 bytes U+10000 */
        {
            "\xF0\x90\x80\x80",
            "\xF0\x90\x80\x80",
            "\\uD800\\uDC00",
        },
        /* 2.1.5  5 bytes U+200000 */
        {
            "\xF8\x88\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 2.1.6  6 bytes U+4000000 */
        {
            "\xFC\x84\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 2.2  Last possible sequence of a certain length */
        /* 2.2.1  1 byte U+007F */
        {
            "\x7F",
            "\x7F",
            "\\u007F",
        },
        /* 2.2.2  2 bytes U+07FF */
        {
            "\xDF\xBF",
            "\xDF\xBF",
            "\\u07FF",
        },
        /*
         * 2.2.3  3 bytes U+FFFC
         * The last possible sequence is actually U+FFFF.  But that's
         * a noncharacter, and already covered by its own test case
         * under 5.3.  Same for U+FFFE.  U+FFFD is the last character
         * in the BMP, and covered under 2.3.  Because of U+FFFD's
         * special role as replacement character, it's worth testing
         * U+FFFC here.
         */
        {
            "\xEF\xBF\xBC",
            "\xEF\xBF\xBC",
            "\\uFFFC",
        },
        /* 2.2.4  4 bytes U+1FFFFF */
        {
            "\xF7\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 2.2.5  5 bytes U+3FFFFFF */
        {
            "\xFB\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 2.2.6  6 bytes U+7FFFFFFF */
        {
            "\xFD\xBF\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 2.3  Other boundary conditions */
        {
            /* last one before surrogate range: U+D7FF */
            "\xED\x9F\xBF",
            "\xED\x9F\xBF",
            "\\uD7FF",
        },
        {
            /* first one after surrogate range: U+E000 */
            "\xEE\x80\x80",
            "\xEE\x80\x80",
            "\\uE000",
        },
        {
            /* last one in BMP: U+FFFD */
            "\xEF\xBF\xBD",
            "\xEF\xBF\xBD",
            "\\uFFFD",
        },
        {
            /* last one in last plane: U+10FFFD */
            "\xF4\x8F\xBF\xBD",
            "\xF4\x8F\xBF\xBD",
            "\\uDBFF\\uDFFD"
        },
        {
            /* first one beyond Unicode range: U+110000 */
            "\xF4\x90\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3  Malformed sequences */
        /* 3.1  Unexpected continuation bytes */
        /* 3.1.1  First continuation byte */
        {
            "\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3.1.2  Last continuation byte */
        {
            "\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 3.1.3  2 continuation bytes */
        {
            "\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        /* 3.1.4  3 continuation bytes */
        {
            "\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.1.5  4 continuation bytes */
        {
            "\x80\xBF\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.1.6  5 continuation bytes */
        {
            "\x80\xBF\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.1.7  6 continuation bytes */
        {
            "\x80\xBF\x80\xBF\x80\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.1.8  7 continuation bytes */
        {
            "\x80\xBF\x80\xBF\x80\xBF\x80",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.1.9  Sequence of all 64 possible continuation bytes */
        {
            "\x80\x81\x82\x83\x84\x85\x86\x87"
            "\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F"
            "\x90\x91\x92\x93\x94\x95\x96\x97"
            "\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F"
            "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7"
            "\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF"
            "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7"
            "\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.2  Lonely start characters */
        /* 3.2.1  All 32 first bytes of 2-byte sequences, followed by space */
        {
            "\xC0 \xC1 \xC2 \xC3 \xC4 \xC5 \xC6 \xC7 "
            "\xC8 \xC9 \xCA \xCB \xCC \xCD \xCE \xCF "
            "\xD0 \xD1 \xD2 \xD3 \xD4 \xD5 \xD6 \xD7 "
            "\xD8 \xD9 \xDA \xDB \xDC \xDD \xDE \xDF ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        /* 3.2.2  All 16 first bytes of 3-byte sequences, followed by space */
        {
            "\xE0 \xE1 \xE2 \xE3 \xE4 \xE5 \xE6 \xE7 "
            "\xE8 \xE9 \xEA \xEB \xEC \xED \xEE \xEF ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD "
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        /* 3.2.3  All 8 first bytes of 4-byte sequences, followed by space */
        {
            "\xF0 \xF1 \xF2 \xF3 \xF4 \xF5 \xF6 \xF7 ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        /* 3.2.4  All 4 first bytes of 5-byte sequences, followed by space */
        {
            "\xF8 \xF9 \xFA \xFB ",
            NULL,
            "\\uFFFD \\uFFFD \\uFFFD \\uFFFD ",
        },
        /* 3.2.5  All 2 first bytes of 6-byte sequences, followed by space */
        {
            "\xFC \xFD ",
            NULL,
            "\\uFFFD \\uFFFD ",
        },
        /* 3.3  Sequences with last continuation byte missing */
        /* 3.3.1  2-byte sequence with last byte missing (U+0000) */
        {
            "\xC0",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.2  3-byte sequence with last byte missing (U+0000) */
        {
            "\xE0\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.3  4-byte sequence with last byte missing (U+0000) */
        {
            "\xF0\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.4  5-byte sequence with last byte missing (U+0000) */
        {
            "\xF8\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.5  6-byte sequence with last byte missing (U+0000) */
        {
            "\xFC\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.6  2-byte sequence with last byte missing (U+07FF) */
        {
            "\xDF",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.7  3-byte sequence with last byte missing (U+FFFF) */
        {
            "\xEF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.8  4-byte sequence with last byte missing (U+1FFFFF) */
        {
            "\xF7\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.9  5-byte sequence with last byte missing (U+3FFFFFF) */
        {
            "\xFB\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 3.3.10  6-byte sequence with last byte missing (U+7FFFFFFF) */
        {
            "\xFD\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 3.4  Concatenation of incomplete sequences */
        {
            "\xC0\xE0\x80\xF0\x80\x80\xF8\x80\x80\x80\xFC\x80\x80\x80\x80"
            "\xDF\xEF\xBF\xF7\xBF\xBF\xFB\xBF\xBF\xBF\xFD\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 3.5  Impossible bytes */
        {
            "\xFE",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFE\xFE\xFF\xFF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        /* 4  Overlong sequences */
        /* 4.1  Overlong '/' */
        {
            "\xC0\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xE0\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF0\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xF8\x80\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        {
            "\xFC\x80\x80\x80\x80\xAF",
            NULL,
            "\\uFFFD",
        },
        /*
         * 4.2  Maximum overlong sequences
         * Highest Unicode value that is still resulting in an
         * overlong sequence if represented with the given number of
         * bytes.  This is a boundary test for safe UTF-8 decoders.
         */
        {
            /* \U+007F */
            "\xC1\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+07FF */
            "\xE0\x9F\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /*
             * \U+FFFC
             * The actual maximum would be U+FFFF, but that's a
             * noncharacter.  Testing U+FFFC seems more useful.  See
             * also 2.2.3
             */
            "\xF0\x8F\xBF\xBC",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+1FFFFF */
            "\xF8\x87\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+3FFFFFF */
            "\xFC\x83\xBF\xBF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 4.3  Overlong representation of the NUL character */
        {
            /* \U+0000 */
            "\xC0\x80",
            "\xC0\x80",
            "\\u0000",
        },
        {
            /* \U+0000 */
            "\xE0\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+0000 */
            "\xF0\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+0000 */
            "\xF8\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+0000 */
            "\xFC\x80\x80\x80\x80\x80",
            NULL,
            "\\uFFFD",
        },
        /* 5  Illegal code positions */
        /* 5.1  Single UTF-16 surrogates */
        {
            /* \U+D800 */
            "\xED\xA0\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DB7F */
            "\xED\xAD\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DB80 */
            "\xED\xAE\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DBFF */
            "\xED\xAF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DC00 */
            "\xED\xB0\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DF80 */
            "\xED\xBE\x80",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+DFFF */
            "\xED\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        /* 5.2  Paired UTF-16 surrogates */
        {
            /* \U+D800\U+DC00 */
            "\xED\xA0\x80\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+D800\U+DFFF */
            "\xED\xA0\x80\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DB7F\U+DC00 */
            "\xED\xAD\xBF\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DB7F\U+DFFF */
            "\xED\xAD\xBF\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DB80\U+DC00 */
            "\xED\xAE\x80\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DB80\U+DFFF */
            "\xED\xAE\x80\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DBFF\U+DC00 */
            "\xED\xAF\xBF\xED\xB0\x80",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        {
            /* \U+DBFF\U+DFFF */
            "\xED\xAF\xBF\xED\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD",
        },
        /* 5.3  Other illegal code positions */
        /* BMP noncharacters */
        {
            /* \U+FFFE */
            "\xEF\xBF\xBE",
            NULL,
            "\\uFFFD",
        },
        {
            /* \U+FFFF */
            "\xEF\xBF\xBF",
            NULL,
            "\\uFFFD",
        },
        {
            /* U+FDD0 */
            "\xEF\xB7\x90",
            NULL,
            "\\uFFFD",
        },
        {
            /* U+FDEF */
            "\xEF\xB7\xAF",
            NULL,
            "\\uFFFD",
        },
        /* Plane 1 .. 16 noncharacters */
        {
            /* U+1FFFE U+1FFFF U+2FFFE U+2FFFF ... U+10FFFE U+10FFFF */
            "\xF0\x9F\xBF\xBE\xF0\x9F\xBF\xBF"
            "\xF0\xAF\xBF\xBE\xF0\xAF\xBF\xBF"
            "\xF0\xBF\xBF\xBE\xF0\xBF\xBF\xBF"
            "\xF1\x8F\xBF\xBE\xF1\x8F\xBF\xBF"
            "\xF1\x9F\xBF\xBE\xF1\x9F\xBF\xBF"
            "\xF1\xAF\xBF\xBE\xF1\xAF\xBF\xBF"
            "\xF1\xBF\xBF\xBE\xF1\xBF\xBF\xBF"
            "\xF2\x8F\xBF\xBE\xF2\x8F\xBF\xBF"
            "\xF2\x9F\xBF\xBE\xF2\x9F\xBF\xBF"
            "\xF2\xAF\xBF\xBE\xF2\xAF\xBF\xBF"
            "\xF2\xBF\xBF\xBE\xF2\xBF\xBF\xBF"
            "\xF3\x8F\xBF\xBE\xF3\x8F\xBF\xBF"
            "\xF3\x9F\xBF\xBE\xF3\x9F\xBF\xBF"
            "\xF3\xAF\xBF\xBE\xF3\xAF\xBF\xBF"
            "\xF3\xBF\xBF\xBE\xF3\xBF\xBF\xBF"
            "\xF4\x8F\xBF\xBE\xF4\x8F\xBF\xBF",
            NULL,
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD"
            "\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD\\uFFFD",
        },
        {}
    };
    int i, j;
    QString *str;
    const char *json_in, *utf8_out, *utf8_in, *json_out, *tail;
    char *end, *in, *jstr;

    for (i = 0; test_cases[i].json_in; i++) {
        for (j = 0; j < 2; j++) {
            json_in = test_cases[i].json_in;
            utf8_out = test_cases[i].utf8_out;
            utf8_in = test_cases[i].utf8_out ?: test_cases[i].json_in;
            json_out = test_cases[i].json_out ?: test_cases[i].json_in;

            /* Parse @json_in, expect @utf8_out */
            if (utf8_out) {
                str = from_json_str(json_in, j, &error_abort);
                g_assert_cmpstr(qstring_get_try_str(str), ==, utf8_out);
                qobject_unref(str);
            } else {
                str = from_json_str(json_in, j, NULL);
                g_assert(!str);
                /*
                 * Failure may be due to any sequence, but *all* sequences
                 * are expected to fail.  Test each one in isolation.
                 */
                for (tail = json_in; *tail; tail = end) {
                    mod_utf8_codepoint(tail, 6, &end);
                    if (*end == ' ') {
                        end++;
                    }
                    in = strndup(tail, end - tail);
                    str = from_json_str(in, j, NULL);
                    g_assert(!str);
                    g_free(in);
                }
            }

            /* Unparse @utf8_in, expect @json_out */
            str = qstring_from_str(utf8_in);
            jstr = to_json_str(str);
            g_assert_cmpstr(jstr, ==, json_out);
            qobject_unref(str);
            g_free(jstr);

            /* Parse @json_out right back, unless it has replacements */
            if (!strstr(json_out, "\\uFFFD")) {
                str = from_json_str(json_out, j, &error_abort);
                g_assert_cmpstr(qstring_get_try_str(str), ==, utf8_in);
                qobject_unref(str);
            }
        }
    }
}

static void simple_number(void)
{
    int i;
    struct {
        const char *encoded;
        int64_t decoded;
        int skip;
    } test_cases[] = {
        { "0", 0 },
        { "1234", 1234 },
        { "1", 1 },
        { "-32", -32 },
        { "-0", 0, .skip = 1 },
        { },
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QNum *qnum;
        int64_t val;

        qnum = qobject_to(QNum,
                          qobject_from_json(test_cases[i].encoded,
                                            &error_abort));
        g_assert(qnum);
        g_assert(qnum_get_try_int(qnum, &val));
        g_assert_cmpint(val, ==, test_cases[i].decoded);
        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(QOBJECT(qnum));
            g_assert(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            qobject_unref(str);
        }

        qobject_unref(qnum);
    }
}

static void large_number(void)
{
    const char *maxu64 = "18446744073709551615"; /* 2^64-1 */
    const char *gtu64 = "18446744073709551616"; /* 2^64 */
    const char *lti64 = "-9223372036854775809"; /* -2^63 - 1 */
    QNum *qnum;
    QString *str;
    uint64_t val;
    int64_t ival;

    qnum = qobject_to(QNum, qobject_from_json(maxu64, &error_abort));
    g_assert(qnum);
    g_assert_cmpuint(qnum_get_uint(qnum), ==, 18446744073709551615U);
    g_assert(!qnum_get_try_int(qnum, &ival));

    str = qobject_to_json(QOBJECT(qnum));
    g_assert_cmpstr(qstring_get_str(str), ==, maxu64);
    qobject_unref(str);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_json(gtu64, &error_abort));
    g_assert(qnum);
    g_assert_cmpfloat(qnum_get_double(qnum), ==, 18446744073709552e3);
    g_assert(!qnum_get_try_uint(qnum, &val));
    g_assert(!qnum_get_try_int(qnum, &ival));

    str = qobject_to_json(QOBJECT(qnum));
    g_assert_cmpstr(qstring_get_str(str), ==, gtu64);
    qobject_unref(str);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_json(lti64, &error_abort));
    g_assert(qnum);
    g_assert_cmpfloat(qnum_get_double(qnum), ==, -92233720368547758e2);
    g_assert(!qnum_get_try_uint(qnum, &val));
    g_assert(!qnum_get_try_int(qnum, &ival));

    str = qobject_to_json(QOBJECT(qnum));
    g_assert_cmpstr(qstring_get_str(str), ==, "-9223372036854775808");
    qobject_unref(str);
    qobject_unref(qnum);
}

static void float_number(void)
{
    int i;
    struct {
        const char *encoded;
        double decoded;
        int skip;
    } test_cases[] = {
        { "32.43", 32.43 },
        { "0.222", 0.222 },
        { "-32.12313", -32.12313 },
        { "-32.20e-10", -32.20e-10, .skip = 1 },
        { },
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QNum *qnum;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        qnum = qobject_to(QNum, obj);
        g_assert(qnum);
        g_assert(qnum_get_double(qnum) == test_cases[i].decoded);

        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(obj);
            g_assert(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            qobject_unref(str);
        }

        qobject_unref(qnum);
    }
}

static void keyword_literal(void)
{
    QObject *obj;
    QBool *qbool;
    QNull *null;
    QString *str;

    obj = qobject_from_json("true", &error_abort);
    qbool = qobject_to(QBool, obj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);

    str = qobject_to_json(obj);
    g_assert(strcmp(qstring_get_str(str), "true") == 0);
    qobject_unref(str);

    qobject_unref(qbool);

    obj = qobject_from_json("false", &error_abort);
    qbool = qobject_to(QBool, obj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == false);

    str = qobject_to_json(obj);
    g_assert(strcmp(qstring_get_str(str), "false") == 0);
    qobject_unref(str);

    qobject_unref(qbool);

    obj = qobject_from_json("null", &error_abort);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QNULL);

    null = qnull();
    g_assert(QOBJECT(null) == obj);

    qobject_unref(obj);
    qobject_unref(null);
}

static void interpolation_valid(void)
{
    long long value_lld = 0x123456789abcdefLL;
    int64_t value_d64 = value_lld;
    long value_ld = (long)value_lld;
    int value_d = (int)value_lld;
    unsigned long long value_llu = 0xfedcba9876543210ULL;
    uint64_t value_u64 = value_llu;
    unsigned long value_lu = (unsigned long)value_llu;
    unsigned value_u = (unsigned)value_llu;
    double value_f = 2.323423423;
    const char *value_s = "hello world";
    QObject *value_p = QOBJECT(qnull());
    QBool *qbool;
    QNum *qnum;
    QString *qstr;
    QObject *qobj;

    /* bool */

    qbool = qobject_to(QBool, qobject_from_jsonf_nofail("%i", false));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == false);
    qobject_unref(qbool);

    /* Test that non-zero values other than 1 get collapsed to true */
    qbool = qobject_to(QBool, qobject_from_jsonf_nofail("%i", 2));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qobject_unref(qbool);

    /* number */

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%d", value_d));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_d);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%ld", value_ld));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_ld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%lld", value_lld));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_lld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%" PRId64, value_d64));
    g_assert_cmpint(qnum_get_int(qnum), ==, value_lld);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%u", value_u));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_u);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%lu", value_lu));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_lu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%llu", value_llu));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_llu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%" PRIu64, value_u64));
    g_assert_cmpuint(qnum_get_uint(qnum), ==, value_llu);
    qobject_unref(qnum);

    qnum = qobject_to(QNum, qobject_from_jsonf_nofail("%f", value_f));
    g_assert(qnum_get_double(qnum) == value_f);
    qobject_unref(qnum);

    /* string */

    qstr = qobject_to(QString,
                     qobject_from_jsonf_nofail("%s", value_s));
    g_assert_cmpstr(qstring_get_try_str(qstr), ==, value_s);
    qobject_unref(qstr);

    /* object */

    qobj = qobject_from_jsonf_nofail("%p", value_p);
    g_assert(qobj == value_p);
}

static void interpolation_unknown(void)
{
    if (g_test_subprocess()) {
        qobject_from_jsonf_nofail("%x", 666);
    }
    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*Unexpected error*"
                              "invalid interpolation '%x'*");
}

static void interpolation_string(void)
{
    if (g_test_subprocess()) {
        qobject_from_jsonf_nofail("['%s', %s]", "eins", "zwei");
    }
    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*Unexpected error*"
                              "can't interpolate into string*");
}

static void simple_dict(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = "{\"foo\": 42, \"bar\": \"hello world\"}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { "foo", QLIT_QNUM(42) },
                        { "bar", QLIT_QSTR("hello world") },
                        { }
                    })),
        }, {
            .encoded = "{}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { }
                    })),
        }, {
            .encoded = "{\"foo\": 43}",
            .decoded = QLIT_QDICT(((QLitDictEntry[]){
                        { "foo", QLIT_QNUM(43) },
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(qstring_get_str(str), &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));
        qobject_unref(obj);
        qobject_unref(str);
    }
}

/*
 * this generates json of the form:
 * a(0,m) = [0, 1, ..., m-1]
 * a(n,m) = {
 *            'key0': a(0,m),
 *            'key1': a(1,m),
 *            ...
 *            'key(n-1)': a(n-1,m)
 *          }
 */
static void gen_test_json(GString *gstr, int nest_level_max,
                          int elem_count)
{
    int i;

    g_assert(gstr);
    if (nest_level_max == 0) {
        g_string_append(gstr, "[");
        for (i = 0; i < elem_count; i++) {
            g_string_append_printf(gstr, "%d", i);
            if (i < elem_count - 1) {
                g_string_append_printf(gstr, ", ");
            }
        }
        g_string_append(gstr, "]");
        return;
    }

    g_string_append(gstr, "{");
    for (i = 0; i < nest_level_max; i++) {
        g_string_append_printf(gstr, "'key%d': ", i);
        gen_test_json(gstr, i, elem_count);
        if (i < nest_level_max - 1) {
            g_string_append(gstr, ",");
        }
    }
    g_string_append(gstr, "}");
}

static void large_dict(void)
{
    GString *gstr = g_string_new("");
    QObject *obj;

    gen_test_json(gstr, 10, 100);
    obj = qobject_from_json(gstr->str, &error_abort);
    g_assert(obj != NULL);

    qobject_unref(obj);
    g_string_free(gstr, true);
}

static void simple_list(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = "[43,42]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = "[43]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        { }
                    })),
        },
        {
            .encoded = "[]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        { }
                    })),
        },
        {
            .encoded = "[{}]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QDICT(((QLitDictEntry[]){
                                    {},
                                        })),
                        {},
                            })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(qstring_get_str(str), &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));
        qobject_unref(obj);
        qobject_unref(str);
    }
}

static void simple_whitespace(void)
{
    int i;
    struct {
        const char *encoded;
        QLitObject decoded;
    } test_cases[] = {
        {
            .encoded = " [ 43 , 42 ]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = "\t[ 43 , { 'h' : 'b' },\r\n\t[ ], 42 ]\n",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QDICT(((QLitDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { }})),
                        QLIT_QLIST(((QLitObject[]){
                                    { }})),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        {
            .encoded = " [ 43 , { 'h' : 'b' , 'a' : 32 }, [ ], 42 ]",
            .decoded = QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(43),
                        QLIT_QDICT(((QLitDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { "a", QLIT_QNUM(32) },
                                    { }})),
                        QLIT_QLIST(((QLitObject[]){
                                    { }})),
                        QLIT_QNUM(42),
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded, &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        str = qobject_to_json(obj);
        qobject_unref(obj);

        obj = qobject_from_json(qstring_get_str(str), &error_abort);
        g_assert(qlit_equal_qobject(&test_cases[i].decoded, obj));

        qobject_unref(obj);
        qobject_unref(str);
    }
}

static void simple_interpolation(void)
{
    QObject *embedded_obj;
    QObject *obj;
    QLitObject decoded = QLIT_QLIST(((QLitObject[]){
            QLIT_QNUM(1),
            QLIT_QSTR("100%"),
            QLIT_QLIST(((QLitObject[]){
                        QLIT_QNUM(32),
                        QLIT_QNUM(42),
                        {}})),
            {}}));

    embedded_obj = qobject_from_json("[32, 42]", &error_abort);
    g_assert(embedded_obj != NULL);

    obj = qobject_from_jsonf_nofail("[%d, '100%%', %p]", 1, embedded_obj);
    g_assert(qlit_equal_qobject(&decoded, obj));

    qobject_unref(obj);
}

static void empty_input(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void blank_input(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("\n ", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void junk_input(void)
{
    /* Note: junk within strings is covered elsewhere */
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("@", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("{\x01", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("[0\xFF]", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("00", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("[1e", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("truer", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_string(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("\"abc", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_sq_string(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("'abc", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_escape(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("\"abc\\\"", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_array(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_array_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32,", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void invalid_array_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("[32,}", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_dict(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_dict_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32,", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void invalid_dict_comma(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("{'abc':32,}", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void unterminated_literal(void)
{
    Error *err = NULL;
    QObject *obj = qobject_from_json("nul", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static char *make_nest(char *buf, size_t cnt)
{
    memset(buf, '[', cnt - 1);
    buf[cnt - 1] = '{';
    buf[cnt] = '}';
    memset(buf + cnt + 1, ']', cnt - 1);
    buf[2 * cnt] = 0;
    return buf;
}

static void limits_nesting(void)
{
    Error *err = NULL;
    enum { max_nesting = 1024 }; /* see qobject/json-streamer.c */
    char buf[2 * (max_nesting + 1) + 1];
    QObject *obj;

    obj = qobject_from_json(make_nest(buf, max_nesting), &error_abort);
    g_assert(obj != NULL);
    qobject_unref(obj);

    obj = qobject_from_json(make_nest(buf, max_nesting + 1), &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

static void multiple_values(void)
{
    Error *err = NULL;
    QObject *obj;

    obj = qobject_from_json("false true", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);

    obj = qobject_from_json("} true", &err);
    error_free_or_abort(&err);
    g_assert(obj == NULL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/literals/string/escaped", escaped_string);
    g_test_add_func("/literals/string/quotes", string_with_quotes);
    g_test_add_func("/literals/string/utf8", utf8_string);

    g_test_add_func("/literals/number/simple", simple_number);
    g_test_add_func("/literals/number/large", large_number);
    g_test_add_func("/literals/number/float", float_number);

    g_test_add_func("/literals/keyword", keyword_literal);

    g_test_add_func("/literals/interpolation/valid", interpolation_valid);
    g_test_add_func("/literals/interpolation/unkown", interpolation_unknown);
    g_test_add_func("/literals/interpolation/string", interpolation_string);

    g_test_add_func("/dicts/simple_dict", simple_dict);
    g_test_add_func("/dicts/large_dict", large_dict);
    g_test_add_func("/lists/simple_list", simple_list);

    g_test_add_func("/mixed/simple_whitespace", simple_whitespace);
    g_test_add_func("/mixed/interpolation", simple_interpolation);

    g_test_add_func("/errors/empty", empty_input);
    g_test_add_func("/errors/blank", blank_input);
    g_test_add_func("/errors/junk", junk_input);
    g_test_add_func("/errors/unterminated/string", unterminated_string);
    g_test_add_func("/errors/unterminated/escape", unterminated_escape);
    g_test_add_func("/errors/unterminated/sq_string", unterminated_sq_string);
    g_test_add_func("/errors/unterminated/array", unterminated_array);
    g_test_add_func("/errors/unterminated/array_comma", unterminated_array_comma);
    g_test_add_func("/errors/unterminated/dict", unterminated_dict);
    g_test_add_func("/errors/unterminated/dict_comma", unterminated_dict_comma);
    g_test_add_func("/errors/invalid_array_comma", invalid_array_comma);
    g_test_add_func("/errors/invalid_dict_comma", invalid_dict_comma);
    g_test_add_func("/errors/unterminated/literal", unterminated_literal);
    g_test_add_func("/errors/limits/nesting", limits_nesting);
    g_test_add_func("/errors/multiple_values", multiple_values);

    return g_test_run();
}
