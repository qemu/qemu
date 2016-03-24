/*
 * Dealing with Unicode
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/unicode.h"

/**
 * mod_utf8_codepoint:
 * @s: string encoded in modified UTF-8
 * @n: maximum number of bytes to read from @s, if less than 6
 * @end: set to end of sequence on return
 *
 * Convert the modified UTF-8 sequence at the start of @s.  Modified
 * UTF-8 is exactly like UTF-8, except U+0000 is encoded as
 * "\xC0\x80".
 *
 * If @n is zero or @s points to a zero byte, the sequence is invalid,
 * and @end is set to @s.
 *
 * If @s points to an impossible byte (0xFE or 0xFF) or a continuation
 * byte, the sequence is invalid, and @end is set to @s + 1
 *
 * Else, the first byte determines how many continuation bytes are
 * expected.  If there are fewer, the sequence is invalid, and @end is
 * set to @s + 1 + actual number of continuation bytes.  Else, the
 * sequence is well-formed, and @end is set to @s + 1 + expected
 * number of continuation bytes.
 *
 * A well-formed sequence is valid unless it encodes a codepoint
 * outside the Unicode range U+0000..U+10FFFF, one of Unicode's 66
 * noncharacters, a surrogate codepoint, or is overlong.  Except the
 * overlong sequence "\xC0\x80" is valid.
 *
 * Conversion succeeds if and only if the sequence is valid.
 *
 * Returns: the Unicode codepoint on success, -1 on failure.
 */
int mod_utf8_codepoint(const char *s, size_t n, char **end)
{
    static int min_cp[5] = { 0x80, 0x800, 0x10000, 0x200000, 0x4000000 };
    const unsigned char *p;
    unsigned byte, mask, len, i;
    int cp;

    if (n == 0 || *s == 0) {
        /* empty sequence */
        *end = (char *)s;
        return -1;
    }

    p = (const unsigned char *)s;
    byte = *p++;
    if (byte < 0x80) {
        cp = byte;              /* one byte sequence */
    } else if (byte >= 0xFE) {
        cp = -1;                /* impossible bytes 0xFE, 0xFF */
    } else if ((byte & 0x40) == 0) {
        cp = -1;                /* unexpected continuation byte */
    } else {
        /* multi-byte sequence */
        len = 0;
        for (mask = 0x80; byte & mask; mask >>= 1) {
            len++;
        }
        assert(len > 1 && len < 7);
        cp = byte & (mask - 1);
        for (i = 1; i < len; i++) {
            byte = i < n ? *p : 0;
            if ((byte & 0xC0) != 0x80) {
                cp = -1;        /* continuation byte missing */
                goto out;
            }
            p++;
            cp <<= 6;
            cp |= byte & 0x3F;
        }
        if (cp > 0x10FFFF) {
            cp = -1;            /* beyond Unicode range */
        } else if ((cp >= 0xFDD0 && cp <= 0xFDEF)
                   || (cp & 0xFFFE) == 0xFFFE) {
            cp = -1;            /* noncharacter */
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = -1;            /* surrogate code point */
        } else if (cp < min_cp[len - 2] && !(cp == 0 && len == 2)) {
            cp = -1;            /* overlong, not \xC0\x80 */
        }
    }

out:
    *end = (char *)p;
    return cp;
}
