/**
 * uri.c: set of generic URI related routines
 *
 * Reference: RFCs 3986, 2732 and 2373
 *
 * Copyright (C) 1998-2003 Daniel Veillard.  All Rights Reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * DANIEL VEILLARD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Daniel Veillard shall not
 * be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from him.
 *
 * daniel@veillard.com
 *
 **
 *
 * Copyright (C) 2007, 2009-2010 Red Hat, Inc.
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
 * License along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors:
 *    Richard W.M. Jones <rjones@redhat.com>
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu/uri.h"

static void uri_clean(URI *uri);

/*
 * Old rule from 2396 used in legacy handling code
 * alpha    = lowalpha | upalpha
 */
#define IS_ALPHA(x) (IS_LOWALPHA(x) || IS_UPALPHA(x))

/*
 * lowalpha = "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j" |
 *            "k" | "l" | "m" | "n" | "o" | "p" | "q" | "r" | "s" | "t" |
 *            "u" | "v" | "w" | "x" | "y" | "z"
 */

#define IS_LOWALPHA(x) (((x) >= 'a') && ((x) <= 'z'))

/*
 * upalpha = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J" |
 *           "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" | "S" | "T" |
 *           "U" | "V" | "W" | "X" | "Y" | "Z"
 */
#define IS_UPALPHA(x) (((x) >= 'A') && ((x) <= 'Z'))

#ifdef IS_DIGIT
#undef IS_DIGIT
#endif
/*
 * digit = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
 */
#define IS_DIGIT(x) (((x) >= '0') && ((x) <= '9'))

/*
 * alphanum = alpha | digit
 */

#define IS_ALPHANUM(x) (IS_ALPHA(x) || IS_DIGIT(x))

/*
 * mark = "-" | "_" | "." | "!" | "~" | "*" | "'" | "(" | ")"
 */

#define IS_MARK(x) (((x) == '-') || ((x) == '_') || ((x) == '.') ||            \
    ((x) == '!') || ((x) == '~') || ((x) == '*') || ((x) == '\'') ||           \
    ((x) == '(') || ((x) == ')'))

/*
 * unwise = "{" | "}" | "|" | "\" | "^" | "`"
 */

#define IS_UNWISE(p)                                                           \
    (((*(p) == '{')) || ((*(p) == '}')) || ((*(p) == '|')) ||                  \
     ((*(p) == '\\')) || ((*(p) == '^')) || ((*(p) == '[')) ||                 \
     ((*(p) == ']')) || ((*(p) == '`')))
/*
 * reserved = ";" | "/" | "?" | ":" | "@" | "&" | "=" | "+" | "$" | "," |
 *            "[" | "]"
 */

#define IS_RESERVED(x) (((x) == ';') || ((x) == '/') || ((x) == '?') ||        \
    ((x) == ':') || ((x) == '@') || ((x) == '&') || ((x) == '=') ||            \
    ((x) == '+') || ((x) == '$') || ((x) == ',') || ((x) == '[') ||            \
    ((x) == ']'))

/*
 * unreserved = alphanum | mark
 */

#define IS_UNRESERVED(x) (IS_ALPHANUM(x) || IS_MARK(x))

/*
 * Skip to next pointer char, handle escaped sequences
 */

#define NEXT(p) ((*p == '%') ? p += 3 : p++)

/*
 * Productions from the spec.
 *
 *    authority     = server | reg_name
 *    reg_name      = 1*( unreserved | escaped | "$" | "," |
 *                        ";" | ":" | "@" | "&" | "=" | "+" )
 *
 * path          = [ abs_path | opaque_part ]
 */

/************************************************************************
 *                                                                      *
 *                         RFC 3986 parser                              *
 *                                                                      *
 ************************************************************************/

#define ISA_DIGIT(p) ((*(p) >= '0') && (*(p) <= '9'))
#define ISA_ALPHA(p) (((*(p) >= 'a') && (*(p) <= 'z')) ||                      \
                      ((*(p) >= 'A') && (*(p) <= 'Z')))
#define ISA_HEXDIG(p)                                                          \
    (ISA_DIGIT(p) || ((*(p) >= 'a') && (*(p) <= 'f')) ||                       \
     ((*(p) >= 'A') && (*(p) <= 'F')))

/*
 *    sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
 *                     / "*" / "+" / "," / ";" / "="
 */
#define ISA_SUB_DELIM(p)                                                       \
    (((*(p) == '!')) || ((*(p) == '$')) || ((*(p) == '&')) ||                  \
     ((*(p) == '(')) || ((*(p) == ')')) || ((*(p) == '*')) ||                  \
     ((*(p) == '+')) || ((*(p) == ',')) || ((*(p) == ';')) ||                  \
     ((*(p) == '=')) || ((*(p) == '\'')))

/*
 *    unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
 */
#define ISA_UNRESERVED(p)                                                      \
    ((ISA_ALPHA(p)) || (ISA_DIGIT(p)) || ((*(p) == '-')) ||                    \
     ((*(p) == '.')) || ((*(p) == '_')) || ((*(p) == '~')))

/*
 *    pct-encoded   = "%" HEXDIG HEXDIG
 */
#define ISA_PCT_ENCODED(p)                                                     \
    ((*(p) == '%') && (ISA_HEXDIG(p + 1)) && (ISA_HEXDIG(p + 2)))

/*
 *    pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
 */
#define ISA_PCHAR(p)                                                           \
    (ISA_UNRESERVED(p) || ISA_PCT_ENCODED(p) || ISA_SUB_DELIM(p) ||            \
     ((*(p) == ':')) || ((*(p) == '@')))

/**
 * rfc3986_parse_scheme:
 * @uri:  pointer to an URI structure
 * @str:  pointer to the string to analyze
 *
 * Parse an URI scheme
 *
 * ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_scheme(URI *uri, const char **str)
{
    const char *cur;

    if (str == NULL) {
        return -1;
    }

    cur = *str;
    if (!ISA_ALPHA(cur)) {
        return 2;
    }
    cur++;
    while (ISA_ALPHA(cur) || ISA_DIGIT(cur) || (*cur == '+') || (*cur == '-') ||
           (*cur == '.')) {
        cur++;
    }
    if (uri != NULL) {
        g_free(uri->scheme);
        uri->scheme = g_strndup(*str, cur - *str);
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_fragment:
 * @uri:  pointer to an URI structure
 * @str:  pointer to the string to analyze
 *
 * Parse the query part of an URI
 *
 * fragment      = *( pchar / "/" / "?" )
 * NOTE: the strict syntax as defined by 3986 does not allow '[' and ']'
 *       in the fragment identifier but this is used very broadly for
 *       xpointer scheme selection, so we are allowing it here to not break
 *       for example all the DocBook processing chains.
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_fragment(URI *uri, const char **str)
{
    const char *cur;

    if (str == NULL) {
        return -1;
    }

    cur = *str;

    while ((ISA_PCHAR(cur)) || (*cur == '/') || (*cur == '?') ||
           (*cur == '[') || (*cur == ']') ||
           ((uri != NULL) && (uri->cleanup & 1) && (IS_UNWISE(cur)))) {
        NEXT(cur);
    }
    if (uri != NULL) {
        g_free(uri->fragment);
        if (uri->cleanup & 2) {
            uri->fragment = g_strndup(*str, cur - *str);
        } else {
            uri->fragment = g_uri_unescape_segment(*str, cur, NULL);
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_query:
 * @uri:  pointer to an URI structure
 * @str:  pointer to the string to analyze
 *
 * Parse the query part of an URI
 *
 * query = *uric
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_query(URI *uri, const char **str)
{
    const char *cur;

    if (str == NULL) {
        return -1;
    }

    cur = *str;

    while ((ISA_PCHAR(cur)) || (*cur == '/') || (*cur == '?') ||
           ((uri != NULL) && (uri->cleanup & 1) && (IS_UNWISE(cur)))) {
        NEXT(cur);
    }
    if (uri != NULL) {
        g_free(uri->query);
        uri->query = g_strndup(*str, cur - *str);
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_port:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse a port  part and fills in the appropriate fields
 * of the @uri structure
 *
 * port          = *DIGIT
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_port(URI *uri, const char **str)
{
    const char *cur = *str;
    int port = 0;

    if (ISA_DIGIT(cur)) {
        while (ISA_DIGIT(cur)) {
            port = port * 10 + (*cur - '0');
            if (port > 65535) {
                return 1;
            }
            cur++;
        }
        if (uri) {
            uri->port = port;
        }
        *str = cur;
        return 0;
    }
    return 1;
}

/**
 * rfc3986_parse_user_info:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse a user information part and fill in the appropriate fields
 * of the @uri structure
 *
 * userinfo      = *( unreserved / pct-encoded / sub-delims / ":" )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_user_info(URI *uri, const char **str)
{
    const char *cur;

    cur = *str;
    while (ISA_UNRESERVED(cur) || ISA_PCT_ENCODED(cur) || ISA_SUB_DELIM(cur) ||
           (*cur == ':')) {
        NEXT(cur);
    }
    if (*cur == '@') {
        if (uri != NULL) {
            g_free(uri->user);
            if (uri->cleanup & 2) {
                uri->user = g_strndup(*str, cur - *str);
            } else {
                uri->user = g_uri_unescape_segment(*str, cur, NULL);
            }
        }
        *str = cur;
        return 0;
    }
    return 1;
}

/**
 * rfc3986_parse_dec_octet:
 * @str:  the string to analyze
 *
 *    dec-octet     = DIGIT                 ; 0-9
 *                  / %x31-39 DIGIT         ; 10-99
 *                  / "1" 2DIGIT            ; 100-199
 *                  / "2" %x30-34 DIGIT     ; 200-249
 *                  / "25" %x30-35          ; 250-255
 *
 * Skip a dec-octet.
 *
 * Returns 0 if found and skipped, 1 otherwise
 */
static int rfc3986_parse_dec_octet(const char **str)
{
    const char *cur = *str;

    if (!(ISA_DIGIT(cur))) {
        return 1;
    }
    if (!ISA_DIGIT(cur + 1)) {
        cur++;
    } else if ((*cur != '0') && (ISA_DIGIT(cur + 1)) && (!ISA_DIGIT(cur + 2))) {
        cur += 2;
    } else if ((*cur == '1') && (ISA_DIGIT(cur + 1)) && (ISA_DIGIT(cur + 2))) {
        cur += 3;
    } else if ((*cur == '2') && (*(cur + 1) >= '0') && (*(cur + 1) <= '4') &&
             (ISA_DIGIT(cur + 2))) {
        cur += 3;
    } else if ((*cur == '2') && (*(cur + 1) == '5') && (*(cur + 2) >= '0') &&
             (*(cur + 1) <= '5')) {
        cur += 3;
    } else {
        return 1;
    }
    *str = cur;
    return 0;
}
/**
 * rfc3986_parse_host:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an host part and fills in the appropriate fields
 * of the @uri structure
 *
 * host          = IP-literal / IPv4address / reg-name
 * IP-literal    = "[" ( IPv6address / IPvFuture  ) "]"
 * IPv4address   = dec-octet "." dec-octet "." dec-octet "." dec-octet
 * reg-name      = *( unreserved / pct-encoded / sub-delims )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_host(URI *uri, const char **str)
{
    const char *cur = *str;
    const char *host;

    host = cur;
    /*
     * IPv6 and future addressing scheme are enclosed between brackets
     */
    if (*cur == '[') {
        cur++;
        while ((*cur != ']') && (*cur != 0)) {
            cur++;
        }
        if (*cur != ']') {
            return 1;
        }
        cur++;
        goto found;
    }
    /*
     * try to parse an IPv4
     */
    if (ISA_DIGIT(cur)) {
        if (rfc3986_parse_dec_octet(&cur) != 0) {
            goto not_ipv4;
        }
        if (*cur != '.') {
            goto not_ipv4;
        }
        cur++;
        if (rfc3986_parse_dec_octet(&cur) != 0) {
            goto not_ipv4;
        }
        if (*cur != '.') {
            goto not_ipv4;
        }
        if (rfc3986_parse_dec_octet(&cur) != 0) {
            goto not_ipv4;
        }
        if (*cur != '.') {
            goto not_ipv4;
        }
        if (rfc3986_parse_dec_octet(&cur) != 0) {
            goto not_ipv4;
        }
        goto found;
    not_ipv4:
        cur = *str;
    }
    /*
     * then this should be a hostname which can be empty
     */
    while (ISA_UNRESERVED(cur) || ISA_PCT_ENCODED(cur) || ISA_SUB_DELIM(cur)) {
        NEXT(cur);
    }
found:
    if (uri != NULL) {
        g_free(uri->authority);
        uri->authority = NULL;
        g_free(uri->server);
        if (cur != host) {
            if (uri->cleanup & 2) {
                uri->server = g_strndup(host, cur - host);
            } else {
                uri->server = g_uri_unescape_segment(host, cur, NULL);
            }
        } else {
            uri->server = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_authority:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an authority part and fills in the appropriate fields
 * of the @uri structure
 *
 * authority     = [ userinfo "@" ] host [ ":" port ]
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_authority(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;
    /*
     * try to parse a userinfo and check for the trailing @
     */
    ret = rfc3986_parse_user_info(uri, &cur);
    if ((ret != 0) || (*cur != '@')) {
        cur = *str;
    } else {
        cur++;
    }
    ret = rfc3986_parse_host(uri, &cur);
    if (ret != 0) {
        return ret;
    }
    if (*cur == ':') {
        cur++;
        ret = rfc3986_parse_port(uri, &cur);
        if (ret != 0) {
            return ret;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_segment:
 * @str:  the string to analyze
 * @forbid: an optional forbidden character
 * @empty: allow an empty segment
 *
 * Parse a segment and fills in the appropriate fields
 * of the @uri structure
 *
 * segment       = *pchar
 * segment-nz    = 1*pchar
 * segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
 *               ; non-zero-length segment without any colon ":"
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_segment(const char **str, char forbid, int empty)
{
    const char *cur;

    cur = *str;
    if (!ISA_PCHAR(cur)) {
        if (empty) {
            return 0;
        }
        return 1;
    }
    while (ISA_PCHAR(cur) && (*cur != forbid)) {
        NEXT(cur);
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_path_ab_empty:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an path absolute or empty and fills in the appropriate fields
 * of the @uri structure
 *
 * path-abempty  = *( "/" segment )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_path_ab_empty(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    while (*cur == '/') {
        cur++;
        ret = rfc3986_parse_segment(&cur, 0, 1);
        if (ret != 0) {
            return ret;
        }
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (*str != cur) {
            if (uri->cleanup & 2) {
                uri->path = g_strndup(*str, cur - *str);
            } else {
                uri->path = g_uri_unescape_segment(*str, cur, NULL);
            }
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_path_absolute:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an path absolute and fills in the appropriate fields
 * of the @uri structure
 *
 * path-absolute = "/" [ segment-nz *( "/" segment ) ]
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_path_absolute(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    if (*cur != '/') {
        return 1;
    }
    cur++;
    ret = rfc3986_parse_segment(&cur, 0, 0);
    if (ret == 0) {
        while (*cur == '/') {
            cur++;
            ret = rfc3986_parse_segment(&cur, 0, 1);
            if (ret != 0) {
                return ret;
            }
        }
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2) {
                uri->path = g_strndup(*str, cur - *str);
            } else {
                uri->path = g_uri_unescape_segment(*str, cur, NULL);
            }
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_path_rootless:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an path without root and fills in the appropriate fields
 * of the @uri structure
 *
 * path-rootless = segment-nz *( "/" segment )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_path_rootless(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    ret = rfc3986_parse_segment(&cur, 0, 0);
    if (ret != 0) {
        return ret;
    }
    while (*cur == '/') {
        cur++;
        ret = rfc3986_parse_segment(&cur, 0, 1);
        if (ret != 0) {
            return ret;
        }
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2) {
                uri->path = g_strndup(*str, cur - *str);
            } else {
                uri->path = g_uri_unescape_segment(*str, cur, NULL);
            }
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_path_no_scheme:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an path which is not a scheme and fills in the appropriate fields
 * of the @uri structure
 *
 * path-noscheme = segment-nz-nc *( "/" segment )
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_path_no_scheme(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    ret = rfc3986_parse_segment(&cur, ':', 0);
    if (ret != 0) {
        return ret;
    }
    while (*cur == '/') {
        cur++;
        ret = rfc3986_parse_segment(&cur, 0, 1);
        if (ret != 0) {
            return ret;
        }
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2) {
                uri->path = g_strndup(*str, cur - *str);
            } else {
                uri->path = g_uri_unescape_segment(*str, cur, NULL);
            }
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_hier_part:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an hierarchical part and fills in the appropriate fields
 * of the @uri structure
 *
 * hier-part     = "//" authority path-abempty
 *                / path-absolute
 *                / path-rootless
 *                / path-empty
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_hier_part(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    if ((*cur == '/') && (*(cur + 1) == '/')) {
        cur += 2;
        ret = rfc3986_parse_authority(uri, &cur);
        if (ret != 0) {
            return ret;
        }
        ret = rfc3986_parse_path_ab_empty(uri, &cur);
        if (ret != 0) {
            return ret;
        }
        *str = cur;
        return 0;
    } else if (*cur == '/') {
        ret = rfc3986_parse_path_absolute(uri, &cur);
        if (ret != 0) {
            return ret;
        }
    } else if (ISA_PCHAR(cur)) {
        ret = rfc3986_parse_path_rootless(uri, &cur);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* path-empty is effectively empty */
        if (uri != NULL) {
            g_free(uri->path);
            uri->path = NULL;
        }
    }
    *str = cur;
    return 0;
}

/**
 * rfc3986_parse_relative_ref:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an URI string and fills in the appropriate fields
 * of the @uri structure
 *
 * relative-ref  = relative-part [ "?" query ] [ "#" fragment ]
 * relative-part = "//" authority path-abempty
 *               / path-absolute
 *               / path-noscheme
 *               / path-empty
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_relative_ref(URI *uri, const char *str)
{
    int ret;

    if ((*str == '/') && (*(str + 1) == '/')) {
        str += 2;
        ret = rfc3986_parse_authority(uri, &str);
        if (ret != 0) {
            return ret;
        }
        ret = rfc3986_parse_path_ab_empty(uri, &str);
        if (ret != 0) {
            return ret;
        }
    } else if (*str == '/') {
        ret = rfc3986_parse_path_absolute(uri, &str);
        if (ret != 0) {
            return ret;
        }
    } else if (ISA_PCHAR(str)) {
        ret = rfc3986_parse_path_no_scheme(uri, &str);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* path-empty is effectively empty */
        if (uri != NULL) {
            g_free(uri->path);
            uri->path = NULL;
        }
    }

    if (*str == '?') {
        str++;
        ret = rfc3986_parse_query(uri, &str);
        if (ret != 0) {
            return ret;
        }
    }
    if (*str == '#') {
        str++;
        ret = rfc3986_parse_fragment(uri, &str);
        if (ret != 0) {
            return ret;
        }
    }
    if (*str != 0) {
        uri_clean(uri);
        return 1;
    }
    return 0;
}

/**
 * rfc3986_parse:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an URI string and fills in the appropriate fields
 * of the @uri structure
 *
 * scheme ":" hier-part [ "?" query ] [ "#" fragment ]
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse(URI *uri, const char *str)
{
    int ret;

    ret = rfc3986_parse_scheme(uri, &str);
    if (ret != 0) {
        return ret;
    }
    if (*str != ':') {
        return 1;
    }
    str++;
    ret = rfc3986_parse_hier_part(uri, &str);
    if (ret != 0) {
        return ret;
    }
    if (*str == '?') {
        str++;
        ret = rfc3986_parse_query(uri, &str);
        if (ret != 0) {
            return ret;
        }
    }
    if (*str == '#') {
        str++;
        ret = rfc3986_parse_fragment(uri, &str);
        if (ret != 0) {
            return ret;
        }
    }
    if (*str != 0) {
        uri_clean(uri);
        return 1;
    }
    return 0;
}

/**
 * rfc3986_parse_uri_reference:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an URI reference string and fills in the appropriate fields
 * of the @uri structure
 *
 * URI-reference = URI / relative-ref
 *
 * Returns 0 or the error code
 */
static int rfc3986_parse_uri_reference(URI *uri, const char *str)
{
    int ret;

    if (str == NULL) {
        return -1;
    }
    uri_clean(uri);

    /*
     * Try first to parse absolute refs, then fallback to relative if
     * it fails.
     */
    ret = rfc3986_parse(uri, str);
    if (ret != 0) {
        uri_clean(uri);
        ret = rfc3986_parse_relative_ref(uri, str);
        if (ret != 0) {
            uri_clean(uri);
            return ret;
        }
    }
    return 0;
}

/**
 * uri_parse:
 * @str:  the URI string to analyze
 *
 * Parse an URI based on RFC 3986
 *
 * URI-reference = [ absoluteURI | relativeURI ] [ "#" fragment ]
 *
 * Returns a newly built URI or NULL in case of error
 */
URI *uri_parse(const char *str)
{
    URI *uri;
    int ret;

    if (str == NULL) {
        return NULL;
    }
    uri = uri_new();
    ret = rfc3986_parse_uri_reference(uri, str);
    if (ret) {
        uri_free(uri);
        return NULL;
    }
    return uri;
}

/**
 * uri_parse_into:
 * @uri:  pointer to an URI structure
 * @str:  the string to analyze
 *
 * Parse an URI reference string based on RFC 3986 and fills in the
 * appropriate fields of the @uri structure
 *
 * URI-reference = URI / relative-ref
 *
 * Returns 0 or the error code
 */
int uri_parse_into(URI *uri, const char *str)
{
    return rfc3986_parse_uri_reference(uri, str);
}

/**
 * uri_parse_raw:
 * @str:  the URI string to analyze
 * @raw:  if 1 unescaping of URI pieces are disabled
 *
 * Parse an URI but allows to keep intact the original fragments.
 *
 * URI-reference = URI / relative-ref
 *
 * Returns a newly built URI or NULL in case of error
 */
URI *uri_parse_raw(const char *str, int raw)
{
    URI *uri;
    int ret;

    if (str == NULL) {
        return NULL;
    }
    uri = uri_new();
    if (raw) {
        uri->cleanup |= 2;
    }
    ret = uri_parse_into(uri, str);
    if (ret) {
        uri_free(uri);
        return NULL;
    }
    return uri;
}

/************************************************************************
 *                                                                      *
 *                    Generic URI structure functions                   *
 *                                                                      *
 ************************************************************************/

/**
 * uri_new:
 *
 * Simply creates an empty URI
 *
 * Returns the new structure or NULL in case of error
 */
URI *uri_new(void)
{
    return g_new0(URI, 1);
}

/**
 * realloc2n:
 *
 * Function to handle properly a reallocation when saving an URI
 * Also imposes some limit on the length of an URI string output
 */
static char *realloc2n(char *ret, int *max)
{
    char *temp;
    int tmp;

    tmp = *max * 2;
    temp = g_realloc(ret, (tmp + 1));
    *max = tmp;
    return temp;
}

/**
 * uri_to_string:
 * @uri:  pointer to an URI
 *
 * Save the URI as an escaped string
 *
 * Returns a new string (to be deallocated by caller)
 */
char *uri_to_string(URI *uri)
{
    char *ret = NULL;
    char *temp;
    const char *p;
    int len;
    int max;

    if (uri == NULL) {
        return NULL;
    }

    max = 80;
    ret = g_malloc(max + 1);
    len = 0;

    if (uri->scheme != NULL) {
        p = uri->scheme;
        while (*p != 0) {
            if (len >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            ret[len++] = *p++;
        }
        if (len >= max) {
            temp = realloc2n(ret, &max);
            ret = temp;
        }
        ret[len++] = ':';
    }
    if (uri->opaque != NULL) {
        p = uri->opaque;
        while (*p != 0) {
            if (len + 3 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            if (IS_RESERVED(*(p)) || IS_UNRESERVED(*(p))) {
                ret[len++] = *p++;
            } else {
                int val = *(unsigned char *)p++;
                int hi = val / 0x10, lo = val % 0x10;
                ret[len++] = '%';
                ret[len++] = hi + (hi > 9 ? 'A' - 10 : '0');
                ret[len++] = lo + (lo > 9 ? 'A' - 10 : '0');
            }
        }
    } else {
        if (uri->server != NULL) {
            if (len + 3 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            ret[len++] = '/';
            ret[len++] = '/';
            if (uri->user != NULL) {
                p = uri->user;
                while (*p != 0) {
                    if (len + 3 >= max) {
                        temp = realloc2n(ret, &max);
                        ret = temp;
                    }
                    if ((IS_UNRESERVED(*(p))) || ((*(p) == ';')) ||
                        ((*(p) == ':')) || ((*(p) == '&')) || ((*(p) == '=')) ||
                        ((*(p) == '+')) || ((*(p) == '$')) || ((*(p) == ','))) {
                        ret[len++] = *p++;
                    } else {
                        int val = *(unsigned char *)p++;
                        int hi = val / 0x10, lo = val % 0x10;
                        ret[len++] = '%';
                        ret[len++] = hi + (hi > 9 ? 'A' - 10 : '0');
                        ret[len++] = lo + (lo > 9 ? 'A' - 10 : '0');
                    }
                }
                if (len + 3 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                ret[len++] = '@';
            }
            p = uri->server;
            while (*p != 0) {
                if (len >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                ret[len++] = *p++;
            }
            if (uri->port > 0) {
                if (len + 10 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                len += snprintf(&ret[len], max - len, ":%d", uri->port);
            }
        } else if (uri->authority != NULL) {
            if (len + 3 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            ret[len++] = '/';
            ret[len++] = '/';
            p = uri->authority;
            while (*p != 0) {
                if (len + 3 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                if ((IS_UNRESERVED(*(p))) || ((*(p) == '$')) ||
                    ((*(p) == ',')) || ((*(p) == ';')) || ((*(p) == ':')) ||
                    ((*(p) == '@')) || ((*(p) == '&')) || ((*(p) == '=')) ||
                    ((*(p) == '+'))) {
                    ret[len++] = *p++;
                } else {
                    int val = *(unsigned char *)p++;
                    int hi = val / 0x10, lo = val % 0x10;
                    ret[len++] = '%';
                    ret[len++] = hi + (hi > 9 ? 'A' - 10 : '0');
                    ret[len++] = lo + (lo > 9 ? 'A' - 10 : '0');
                }
            }
        } else if (uri->scheme != NULL) {
            if (len + 3 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            ret[len++] = '/';
            ret[len++] = '/';
        }
        if (uri->path != NULL) {
            p = uri->path;
            /*
             * the colon in file:///d: should not be escaped or
             * Windows accesses fail later.
             */
            if ((uri->scheme != NULL) && (p[0] == '/') &&
                (((p[1] >= 'a') && (p[1] <= 'z')) ||
                 ((p[1] >= 'A') && (p[1] <= 'Z'))) &&
                (p[2] == ':') && (!strcmp(uri->scheme, "file"))) {
                if (len + 3 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                ret[len++] = *p++;
                ret[len++] = *p++;
                ret[len++] = *p++;
            }
            while (*p != 0) {
                if (len + 3 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                if ((IS_UNRESERVED(*(p))) || ((*(p) == '/')) ||
                    ((*(p) == ';')) || ((*(p) == '@')) || ((*(p) == '&')) ||
                    ((*(p) == '=')) || ((*(p) == '+')) || ((*(p) == '$')) ||
                    ((*(p) == ','))) {
                    ret[len++] = *p++;
                } else {
                    int val = *(unsigned char *)p++;
                    int hi = val / 0x10, lo = val % 0x10;
                    ret[len++] = '%';
                    ret[len++] = hi + (hi > 9 ? 'A' - 10 : '0');
                    ret[len++] = lo + (lo > 9 ? 'A' - 10 : '0');
                }
            }
        }
        if (uri->query != NULL) {
            if (len + 1 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            ret[len++] = '?';
            p = uri->query;
            while (*p != 0) {
                if (len + 1 >= max) {
                    temp = realloc2n(ret, &max);
                    ret = temp;
                }
                ret[len++] = *p++;
            }
        }
    }
    if (uri->fragment != NULL) {
        if (len + 3 >= max) {
            temp = realloc2n(ret, &max);
            ret = temp;
        }
        ret[len++] = '#';
        p = uri->fragment;
        while (*p != 0) {
            if (len + 3 >= max) {
                temp = realloc2n(ret, &max);
                ret = temp;
            }
            if ((IS_UNRESERVED(*(p))) || (IS_RESERVED(*(p)))) {
                ret[len++] = *p++;
            } else {
                int val = *(unsigned char *)p++;
                int hi = val / 0x10, lo = val % 0x10;
                ret[len++] = '%';
                ret[len++] = hi + (hi > 9 ? 'A' - 10 : '0');
                ret[len++] = lo + (lo > 9 ? 'A' - 10 : '0');
            }
        }
    }
    if (len >= max) {
        temp = realloc2n(ret, &max);
        ret = temp;
    }
    ret[len] = 0;
    return ret;
}

/**
 * uri_clean:
 * @uri:  pointer to an URI
 *
 * Make sure the URI struct is free of content
 */
static void uri_clean(URI *uri)
{
    if (uri == NULL) {
        return;
    }

    g_free(uri->scheme);
    uri->scheme = NULL;
    g_free(uri->server);
    uri->server = NULL;
    g_free(uri->user);
    uri->user = NULL;
    g_free(uri->path);
    uri->path = NULL;
    g_free(uri->fragment);
    uri->fragment = NULL;
    g_free(uri->opaque);
    uri->opaque = NULL;
    g_free(uri->authority);
    uri->authority = NULL;
    g_free(uri->query);
    uri->query = NULL;
}

/**
 * uri_free:
 * @uri:  pointer to an URI, NULL is ignored
 *
 * Free up the URI struct
 */
void uri_free(URI *uri)
{
    uri_clean(uri);
    g_free(uri);
}

/************************************************************************
 *                                                                      *
 *                           Public functions                           *
 *                                                                      *
 ************************************************************************/

/*
 * Utility functions to help parse and assemble query strings.
 */

struct QueryParams *query_params_new(int init_alloc)
{
    struct QueryParams *ps;

    if (init_alloc <= 0) {
        init_alloc = 1;
    }

    ps = g_new(QueryParams, 1);
    ps->n = 0;
    ps->alloc = init_alloc;
    ps->p = g_new(QueryParam, ps->alloc);

    return ps;
}

/* Ensure there is space to store at least one more parameter
 * at the end of the set.
 */
static int query_params_append(struct QueryParams *ps, const char *name,
                               const char *value)
{
    if (ps->n >= ps->alloc) {
        ps->p = g_renew(QueryParam, ps->p, ps->alloc * 2);
        ps->alloc *= 2;
    }

    ps->p[ps->n].name = g_strdup(name);
    ps->p[ps->n].value = g_strdup(value);
    ps->p[ps->n].ignore = 0;
    ps->n++;

    return 0;
}

void query_params_free(struct QueryParams *ps)
{
    int i;

    for (i = 0; i < ps->n; ++i) {
        g_free(ps->p[i].name);
        g_free(ps->p[i].value);
    }
    g_free(ps->p);
    g_free(ps);
}

struct QueryParams *query_params_parse(const char *query)
{
    struct QueryParams *ps;
    const char *end, *eq;

    ps = query_params_new(0);
    if (!query || query[0] == '\0') {
        return ps;
    }

    while (*query) {
        char *name = NULL, *value = NULL;

        /* Find the next separator, or end of the string. */
        end = strchr(query, '&');
        if (!end) {
            end = qemu_strchrnul(query, ';');
        }

        /* Find the first '=' character between here and end. */
        eq = strchr(query, '=');
        if (eq && eq >= end) {
            eq = NULL;
        }

        /* Empty section (eg. "&&"). */
        if (end == query) {
            goto next;
        }

        /* If there is no '=' character, then we have just "name"
         * and consistent with CGI.pm we assume value is "".
         */
        else if (!eq) {
            name = g_uri_unescape_segment(query, end, NULL);
            value = NULL;
        }
        /* Or if we have "name=" here (works around annoying
         * problem when calling uri_string_unescape with len = 0).
         */
        else if (eq + 1 == end) {
            name = g_uri_unescape_segment(query, eq, NULL);
            value = g_new0(char, 1);
        }
        /* If the '=' character is at the beginning then we have
         * "=value" and consistent with CGI.pm we _ignore_ this.
         */
        else if (query == eq) {
            goto next;
        }

        /* Otherwise it's "name=value". */
        else {
            name = g_uri_unescape_segment(query, eq, NULL);
            value = g_uri_unescape_segment(eq + 1, end, NULL);
        }

        /* Append to the parameter set. */
        query_params_append(ps, name, value);
        g_free(name);
        g_free(value);

    next:
        query = end;
        if (*query) {
            query++; /* skip '&' separator */
        }
    }

    return ps;
}
