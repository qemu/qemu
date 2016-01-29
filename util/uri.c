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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Authors:
 *    Richard W.M. Jones <rjones@redhat.com>
 *
 */

#include "qemu/osdep.h"
#include <glib.h>

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

#define IS_MARK(x) (((x) == '-') || ((x) == '_') || ((x) == '.') ||     \
    ((x) == '!') || ((x) == '~') || ((x) == '*') || ((x) == '\'') ||    \
    ((x) == '(') || ((x) == ')'))

/*
 * unwise = "{" | "}" | "|" | "\" | "^" | "`"
 */

#define IS_UNWISE(p)                                                    \
      (((*(p) == '{')) || ((*(p) == '}')) || ((*(p) == '|')) ||         \
       ((*(p) == '\\')) || ((*(p) == '^')) || ((*(p) == '[')) ||        \
       ((*(p) == ']')) || ((*(p) == '`')))
/*
 * reserved = ";" | "/" | "?" | ":" | "@" | "&" | "=" | "+" | "$" | "," |
 *            "[" | "]"
 */

#define IS_RESERVED(x) (((x) == ';') || ((x) == '/') || ((x) == '?') || \
        ((x) == ':') || ((x) == '@') || ((x) == '&') || ((x) == '=') || \
        ((x) == '+') || ((x) == '$') || ((x) == ',') || ((x) == '[') || \
        ((x) == ']'))

/*
 * unreserved = alphanum | mark
 */

#define IS_UNRESERVED(x) (IS_ALPHANUM(x) || IS_MARK(x))

/*
 * Skip to next pointer char, handle escaped sequences
 */

#define NEXT(p) ((*p == '%')? p += 3 : p++)

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
 *									*
 *                         RFC 3986 parser				*
 *									*
 ************************************************************************/

#define ISA_DIGIT(p) ((*(p) >= '0') && (*(p) <= '9'))
#define ISA_ALPHA(p) (((*(p) >= 'a') && (*(p) <= 'z')) ||		\
                      ((*(p) >= 'A') && (*(p) <= 'Z')))
#define ISA_HEXDIG(p)							\
       (ISA_DIGIT(p) || ((*(p) >= 'a') && (*(p) <= 'f')) ||		\
        ((*(p) >= 'A') && (*(p) <= 'F')))

/*
 *    sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
 *                     / "*" / "+" / "," / ";" / "="
 */
#define ISA_SUB_DELIM(p)						\
      (((*(p) == '!')) || ((*(p) == '$')) || ((*(p) == '&')) ||		\
       ((*(p) == '(')) || ((*(p) == ')')) || ((*(p) == '*')) ||		\
       ((*(p) == '+')) || ((*(p) == ',')) || ((*(p) == ';')) ||		\
       ((*(p) == '=')) || ((*(p) == '\'')))

/*
 *    gen-delims    = ":" / "/" / "?" / "#" / "[" / "]" / "@"
 */
#define ISA_GEN_DELIM(p)						\
      (((*(p) == ':')) || ((*(p) == '/')) || ((*(p) == '?')) ||         \
       ((*(p) == '#')) || ((*(p) == '[')) || ((*(p) == ']')) ||         \
       ((*(p) == '@')))

/*
 *    reserved      = gen-delims / sub-delims
 */
#define ISA_RESERVED(p) (ISA_GEN_DELIM(p) || (ISA_SUB_DELIM(p)))

/*
 *    unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
 */
#define ISA_UNRESERVED(p)						\
      ((ISA_ALPHA(p)) || (ISA_DIGIT(p)) || ((*(p) == '-')) ||		\
       ((*(p) == '.')) || ((*(p) == '_')) || ((*(p) == '~')))

/*
 *    pct-encoded   = "%" HEXDIG HEXDIG
 */
#define ISA_PCT_ENCODED(p)						\
     ((*(p) == '%') && (ISA_HEXDIG(p + 1)) && (ISA_HEXDIG(p + 2)))

/*
 *    pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
 */
#define ISA_PCHAR(p)							\
     (ISA_UNRESERVED(p) || ISA_PCT_ENCODED(p) || ISA_SUB_DELIM(p) ||	\
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
static int
rfc3986_parse_scheme(URI *uri, const char **str) {
    const char *cur;

    if (str == NULL)
	return(-1);

    cur = *str;
    if (!ISA_ALPHA(cur))
	return(2);
    cur++;
    while (ISA_ALPHA(cur) || ISA_DIGIT(cur) ||
           (*cur == '+') || (*cur == '-') || (*cur == '.')) cur++;
    if (uri != NULL) {
        g_free(uri->scheme);
	uri->scheme = g_strndup(*str, cur - *str);
    }
    *str = cur;
    return(0);
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
static int
rfc3986_parse_fragment(URI *uri, const char **str)
{
    const char *cur;

    if (str == NULL)
        return (-1);

    cur = *str;

    while ((ISA_PCHAR(cur)) || (*cur == '/') || (*cur == '?') ||
           (*cur == '[') || (*cur == ']') ||
           ((uri != NULL) && (uri->cleanup & 1) && (IS_UNWISE(cur))))
        NEXT(cur);
    if (uri != NULL) {
        g_free(uri->fragment);
	if (uri->cleanup & 2)
	    uri->fragment = g_strndup(*str, cur - *str);
	else
	    uri->fragment = uri_string_unescape(*str, cur - *str, NULL);
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_query(URI *uri, const char **str)
{
    const char *cur;

    if (str == NULL)
        return (-1);

    cur = *str;

    while ((ISA_PCHAR(cur)) || (*cur == '/') || (*cur == '?') ||
           ((uri != NULL) && (uri->cleanup & 1) && (IS_UNWISE(cur))))
        NEXT(cur);
    if (uri != NULL) {
        g_free(uri->query);
	uri->query = g_strndup (*str, cur - *str);
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_port(URI *uri, const char **str)
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
 * Parse an user informations part and fills in the appropriate fields
 * of the @uri structure
 *
 * userinfo      = *( unreserved / pct-encoded / sub-delims / ":" )
 *
 * Returns 0 or the error code
 */
static int
rfc3986_parse_user_info(URI *uri, const char **str)
{
    const char *cur;

    cur = *str;
    while (ISA_UNRESERVED(cur) || ISA_PCT_ENCODED(cur) ||
           ISA_SUB_DELIM(cur) || (*cur == ':'))
	NEXT(cur);
    if (*cur == '@') {
	if (uri != NULL) {
            g_free(uri->user);
	    if (uri->cleanup & 2)
		uri->user = g_strndup(*str, cur - *str);
	    else
		uri->user = uri_string_unescape(*str, cur - *str, NULL);
	}
	*str = cur;
	return(0);
    }
    return(1);
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
static int
rfc3986_parse_dec_octet(const char **str) {
    const char *cur = *str;

    if (!(ISA_DIGIT(cur)))
        return(1);
    if (!ISA_DIGIT(cur+1))
	cur++;
    else if ((*cur != '0') && (ISA_DIGIT(cur + 1)) && (!ISA_DIGIT(cur+2)))
	cur += 2;
    else if ((*cur == '1') && (ISA_DIGIT(cur + 1)) && (ISA_DIGIT(cur + 2)))
	cur += 3;
    else if ((*cur == '2') && (*(cur + 1) >= '0') &&
	     (*(cur + 1) <= '4') && (ISA_DIGIT(cur + 2)))
	cur += 3;
    else if ((*cur == '2') && (*(cur + 1) == '5') &&
	     (*(cur + 2) >= '0') && (*(cur + 1) <= '5'))
	cur += 3;
    else
        return(1);
    *str = cur;
    return(0);
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
static int
rfc3986_parse_host(URI *uri, const char **str)
{
    const char *cur = *str;
    const char *host;

    host = cur;
    /*
     * IPv6 and future addressing scheme are enclosed between brackets
     */
    if (*cur == '[') {
        cur++;
	while ((*cur != ']') && (*cur != 0))
	    cur++;
	if (*cur != ']')
	    return(1);
	cur++;
	goto found;
    }
    /*
     * try to parse an IPv4
     */
    if (ISA_DIGIT(cur)) {
        if (rfc3986_parse_dec_octet(&cur) != 0)
	    goto not_ipv4;
	if (*cur != '.')
	    goto not_ipv4;
	cur++;
        if (rfc3986_parse_dec_octet(&cur) != 0)
	    goto not_ipv4;
	if (*cur != '.')
	    goto not_ipv4;
        if (rfc3986_parse_dec_octet(&cur) != 0)
	    goto not_ipv4;
	if (*cur != '.')
	    goto not_ipv4;
        if (rfc3986_parse_dec_octet(&cur) != 0)
	    goto not_ipv4;
	goto found;
not_ipv4:
        cur = *str;
    }
    /*
     * then this should be a hostname which can be empty
     */
    while (ISA_UNRESERVED(cur) || ISA_PCT_ENCODED(cur) || ISA_SUB_DELIM(cur))
        NEXT(cur);
found:
    if (uri != NULL) {
        g_free(uri->authority);
	uri->authority = NULL;
        g_free(uri->server);
	if (cur != host) {
	    if (uri->cleanup & 2)
		uri->server = g_strndup(host, cur - host);
	    else
		uri->server = uri_string_unescape(host, cur - host, NULL);
	} else
	    uri->server = NULL;
    }
    *str = cur;
    return(0);
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
static int
rfc3986_parse_authority(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;
    /*
     * try to parse an userinfo and check for the trailing @
     */
    ret = rfc3986_parse_user_info(uri, &cur);
    if ((ret != 0) || (*cur != '@'))
        cur = *str;
    else
        cur++;
    ret = rfc3986_parse_host(uri, &cur);
    if (ret != 0) return(ret);
    if (*cur == ':') {
        cur++;
        ret = rfc3986_parse_port(uri, &cur);
	if (ret != 0) return(ret);
    }
    *str = cur;
    return(0);
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
static int
rfc3986_parse_segment(const char **str, char forbid, int empty)
{
    const char *cur;

    cur = *str;
    if (!ISA_PCHAR(cur)) {
        if (empty)
	    return(0);
	return(1);
    }
    while (ISA_PCHAR(cur) && (*cur != forbid))
        NEXT(cur);
    *str = cur;
    return (0);
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
static int
rfc3986_parse_path_ab_empty(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    while (*cur == '/') {
        cur++;
	ret = rfc3986_parse_segment(&cur, 0, 1);
	if (ret != 0) return(ret);
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (*str != cur) {
            if (uri->cleanup & 2)
                uri->path = g_strndup(*str, cur - *str);
            else
                uri->path = uri_string_unescape(*str, cur - *str, NULL);
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_path_absolute(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    if (*cur != '/')
        return(1);
    cur++;
    ret = rfc3986_parse_segment(&cur, 0, 0);
    if (ret == 0) {
	while (*cur == '/') {
	    cur++;
	    ret = rfc3986_parse_segment(&cur, 0, 1);
	    if (ret != 0) return(ret);
	}
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2)
                uri->path = g_strndup(*str, cur - *str);
            else
                uri->path = uri_string_unescape(*str, cur - *str, NULL);
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_path_rootless(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    ret = rfc3986_parse_segment(&cur, 0, 0);
    if (ret != 0) return(ret);
    while (*cur == '/') {
        cur++;
	ret = rfc3986_parse_segment(&cur, 0, 1);
	if (ret != 0) return(ret);
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2)
                uri->path = g_strndup(*str, cur - *str);
            else
                uri->path = uri_string_unescape(*str, cur - *str, NULL);
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_path_no_scheme(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    ret = rfc3986_parse_segment(&cur, ':', 0);
    if (ret != 0) return(ret);
    while (*cur == '/') {
        cur++;
	ret = rfc3986_parse_segment(&cur, 0, 1);
	if (ret != 0) return(ret);
    }
    if (uri != NULL) {
        g_free(uri->path);
        if (cur != *str) {
            if (uri->cleanup & 2)
                uri->path = g_strndup(*str, cur - *str);
            else
                uri->path = uri_string_unescape(*str, cur - *str, NULL);
        } else {
            uri->path = NULL;
        }
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_hier_part(URI *uri, const char **str)
{
    const char *cur;
    int ret;

    cur = *str;

    if ((*cur == '/') && (*(cur + 1) == '/')) {
        cur += 2;
	ret = rfc3986_parse_authority(uri, &cur);
	if (ret != 0) return(ret);
	ret = rfc3986_parse_path_ab_empty(uri, &cur);
	if (ret != 0) return(ret);
	*str = cur;
	return(0);
    } else if (*cur == '/') {
        ret = rfc3986_parse_path_absolute(uri, &cur);
	if (ret != 0) return(ret);
    } else if (ISA_PCHAR(cur)) {
        ret = rfc3986_parse_path_rootless(uri, &cur);
	if (ret != 0) return(ret);
    } else {
	/* path-empty is effectively empty */
	if (uri != NULL) {
            g_free(uri->path);
	    uri->path = NULL;
	}
    }
    *str = cur;
    return (0);
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
static int
rfc3986_parse_relative_ref(URI *uri, const char *str) {
    int ret;

    if ((*str == '/') && (*(str + 1) == '/')) {
        str += 2;
	ret = rfc3986_parse_authority(uri, &str);
	if (ret != 0) return(ret);
	ret = rfc3986_parse_path_ab_empty(uri, &str);
	if (ret != 0) return(ret);
    } else if (*str == '/') {
	ret = rfc3986_parse_path_absolute(uri, &str);
	if (ret != 0) return(ret);
    } else if (ISA_PCHAR(str)) {
        ret = rfc3986_parse_path_no_scheme(uri, &str);
	if (ret != 0) return(ret);
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
	if (ret != 0) return(ret);
    }
    if (*str == '#') {
	str++;
	ret = rfc3986_parse_fragment(uri, &str);
	if (ret != 0) return(ret);
    }
    if (*str != 0) {
	uri_clean(uri);
	return(1);
    }
    return(0);
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
static int
rfc3986_parse(URI *uri, const char *str) {
    int ret;

    ret = rfc3986_parse_scheme(uri, &str);
    if (ret != 0) return(ret);
    if (*str != ':') {
	return(1);
    }
    str++;
    ret = rfc3986_parse_hier_part(uri, &str);
    if (ret != 0) return(ret);
    if (*str == '?') {
	str++;
	ret = rfc3986_parse_query(uri, &str);
	if (ret != 0) return(ret);
    }
    if (*str == '#') {
	str++;
	ret = rfc3986_parse_fragment(uri, &str);
	if (ret != 0) return(ret);
    }
    if (*str != 0) {
	uri_clean(uri);
	return(1);
    }
    return(0);
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
static int
rfc3986_parse_uri_reference(URI *uri, const char *str) {
    int ret;

    if (str == NULL)
	return(-1);
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
	    return(ret);
	}
    }
    return(0);
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
URI *
uri_parse(const char *str) {
    URI *uri;
    int ret;

    if (str == NULL)
	return(NULL);
    uri = uri_new();
    ret = rfc3986_parse_uri_reference(uri, str);
    if (ret) {
        uri_free(uri);
        return(NULL);
    }
    return(uri);
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
int
uri_parse_into(URI *uri, const char *str) {
    return(rfc3986_parse_uri_reference(uri, str));
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
URI *
uri_parse_raw(const char *str, int raw) {
    URI *uri;
    int ret;

    if (str == NULL)
	return(NULL);
    uri = uri_new();
    if (raw) {
        uri->cleanup |= 2;
    }
    ret = uri_parse_into(uri, str);
    if (ret) {
        uri_free(uri);
        return(NULL);
    }
    return(uri);
}

/************************************************************************
 *									*
 *			Generic URI structure functions			*
 *									*
 ************************************************************************/

/**
 * uri_new:
 *
 * Simply creates an empty URI
 *
 * Returns the new structure or NULL in case of error
 */
URI *
uri_new(void) {
    URI *ret;

    ret = g_new0(URI, 1);
    return(ret);
}

/**
 * realloc2n:
 *
 * Function to handle properly a reallocation when saving an URI
 * Also imposes some limit on the length of an URI string output
 */
static char *
realloc2n(char *ret, int *max) {
    char *temp;
    int tmp;

    tmp = *max * 2;
    temp = g_realloc(ret, (tmp + 1));
    *max = tmp;
    return(temp);
}

/**
 * uri_to_string:
 * @uri:  pointer to an URI
 *
 * Save the URI as an escaped string
 *
 * Returns a new string (to be deallocated by caller)
 */
char *
uri_to_string(URI *uri) {
    char *ret = NULL;
    char *temp;
    const char *p;
    int len;
    int max;

    if (uri == NULL) return(NULL);


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
	    if (IS_RESERVED(*(p)) || IS_UNRESERVED(*(p)))
		ret[len++] = *p++;
	    else {
		int val = *(unsigned char *)p++;
		int hi = val / 0x10, lo = val % 0x10;
		ret[len++] = '%';
		ret[len++] = hi + (hi > 9? 'A'-10 : '0');
		ret[len++] = lo + (lo > 9? 'A'-10 : '0');
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
		    if ((IS_UNRESERVED(*(p))) ||
			((*(p) == ';')) || ((*(p) == ':')) ||
			((*(p) == '&')) || ((*(p) == '=')) ||
			((*(p) == '+')) || ((*(p) == '$')) ||
			((*(p) == ',')))
			ret[len++] = *p++;
		    else {
			int val = *(unsigned char *)p++;
			int hi = val / 0x10, lo = val % 0x10;
			ret[len++] = '%';
			ret[len++] = hi + (hi > 9? 'A'-10 : '0');
			ret[len++] = lo + (lo > 9? 'A'-10 : '0');
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
		if ((IS_UNRESERVED(*(p))) ||
                    ((*(p) == '$')) || ((*(p) == ',')) || ((*(p) == ';')) ||
                    ((*(p) == ':')) || ((*(p) == '@')) || ((*(p) == '&')) ||
                    ((*(p) == '=')) || ((*(p) == '+')))
		    ret[len++] = *p++;
		else {
		    int val = *(unsigned char *)p++;
		    int hi = val / 0x10, lo = val % 0x10;
		    ret[len++] = '%';
		    ret[len++] = hi + (hi > 9? 'A'-10 : '0');
		    ret[len++] = lo + (lo > 9? 'A'-10 : '0');
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
	    if ((uri->scheme != NULL) &&
		(p[0] == '/') &&
		(((p[1] >= 'a') && (p[1] <= 'z')) ||
		 ((p[1] >= 'A') && (p[1] <= 'Z'))) &&
		(p[2] == ':') &&
	        (!strcmp(uri->scheme, "file"))) {
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
	            ((*(p) == ',')))
		    ret[len++] = *p++;
		else {
		    int val = *(unsigned char *)p++;
		    int hi = val / 0x10, lo = val % 0x10;
		    ret[len++] = '%';
		    ret[len++] = hi + (hi > 9? 'A'-10 : '0');
		    ret[len++] = lo + (lo > 9? 'A'-10 : '0');
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
	    if ((IS_UNRESERVED(*(p))) || (IS_RESERVED(*(p))))
		ret[len++] = *p++;
	    else {
		int val = *(unsigned char *)p++;
		int hi = val / 0x10, lo = val % 0x10;
		ret[len++] = '%';
		ret[len++] = hi + (hi > 9? 'A'-10 : '0');
		ret[len++] = lo + (lo > 9? 'A'-10 : '0');
	    }
	}
    }
    if (len >= max) {
        temp = realloc2n(ret, &max);
        ret = temp;
    }
    ret[len] = 0;
    return(ret);
}

/**
 * uri_clean:
 * @uri:  pointer to an URI
 *
 * Make sure the URI struct is free of content
 */
static void
uri_clean(URI *uri) {
    if (uri == NULL) return;

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
 * @uri:  pointer to an URI
 *
 * Free up the URI struct
 */
void
uri_free(URI *uri) {
    uri_clean(uri);
    g_free(uri);
}

/************************************************************************
 *									*
 *			Helper functions				*
 *									*
 ************************************************************************/

/**
 * normalize_uri_path:
 * @path:  pointer to the path string
 *
 * Applies the 5 normalization steps to a path string--that is, RFC 2396
 * Section 5.2, steps 6.c through 6.g.
 *
 * Normalization occurs directly on the string, no new allocation is done
 *
 * Returns 0 or an error code
 */
static int
normalize_uri_path(char *path) {
    char *cur, *out;

    if (path == NULL)
	return(-1);

    /* Skip all initial "/" chars.  We want to get to the beginning of the
     * first non-empty segment.
     */
    cur = path;
    while (cur[0] == '/')
      ++cur;
    if (cur[0] == '\0')
      return(0);

    /* Keep everything we've seen so far.  */
    out = cur;

    /*
     * Analyze each segment in sequence for cases (c) and (d).
     */
    while (cur[0] != '\0') {
	/*
	 * c) All occurrences of "./", where "." is a complete path segment,
	 *    are removed from the buffer string.
	 */
	if ((cur[0] == '.') && (cur[1] == '/')) {
	    cur += 2;
	    /* '//' normalization should be done at this point too */
	    while (cur[0] == '/')
		cur++;
	    continue;
	}

	/*
	 * d) If the buffer string ends with "." as a complete path segment,
	 *    that "." is removed.
	 */
	if ((cur[0] == '.') && (cur[1] == '\0'))
	    break;

	/* Otherwise keep the segment.  */
	while (cur[0] != '/') {
            if (cur[0] == '\0')
              goto done_cd;
	    (out++)[0] = (cur++)[0];
	}
	/* nomalize // */
	while ((cur[0] == '/') && (cur[1] == '/'))
	    cur++;

        (out++)[0] = (cur++)[0];
    }
 done_cd:
    out[0] = '\0';

    /* Reset to the beginning of the first segment for the next sequence.  */
    cur = path;
    while (cur[0] == '/')
      ++cur;
    if (cur[0] == '\0')
	return(0);

    /*
     * Analyze each segment in sequence for cases (e) and (f).
     *
     * e) All occurrences of "<segment>/../", where <segment> is a
     *    complete path segment not equal to "..", are removed from the
     *    buffer string.  Removal of these path segments is performed
     *    iteratively, removing the leftmost matching pattern on each
     *    iteration, until no matching pattern remains.
     *
     * f) If the buffer string ends with "<segment>/..", where <segment>
     *    is a complete path segment not equal to "..", that
     *    "<segment>/.." is removed.
     *
     * To satisfy the "iterative" clause in (e), we need to collapse the
     * string every time we find something that needs to be removed.  Thus,
     * we don't need to keep two pointers into the string: we only need a
     * "current position" pointer.
     */
    while (1) {
        char *segp, *tmp;

        /* At the beginning of each iteration of this loop, "cur" points to
         * the first character of the segment we want to examine.
         */

        /* Find the end of the current segment.  */
        segp = cur;
        while ((segp[0] != '/') && (segp[0] != '\0'))
          ++segp;

        /* If this is the last segment, we're done (we need at least two
         * segments to meet the criteria for the (e) and (f) cases).
         */
        if (segp[0] == '\0')
          break;

        /* If the first segment is "..", or if the next segment _isn't_ "..",
         * keep this segment and try the next one.
         */
        ++segp;
        if (((cur[0] == '.') && (cur[1] == '.') && (segp == cur+3))
            || ((segp[0] != '.') || (segp[1] != '.')
                || ((segp[2] != '/') && (segp[2] != '\0')))) {
          cur = segp;
          continue;
        }

        /* If we get here, remove this segment and the next one and back up
         * to the previous segment (if there is one), to implement the
         * "iteratively" clause.  It's pretty much impossible to back up
         * while maintaining two pointers into the buffer, so just compact
         * the whole buffer now.
         */

        /* If this is the end of the buffer, we're done.  */
        if (segp[2] == '\0') {
          cur[0] = '\0';
          break;
        }
        /* Valgrind complained, strcpy(cur, segp + 3); */
        /* string will overlap, do not use strcpy */
        tmp = cur;
        segp += 3;
        while ((*tmp++ = *segp++) != 0)
          ;

        /* If there are no previous segments, then keep going from here.  */
        segp = cur;
        while ((segp > path) && ((--segp)[0] == '/'))
          ;
        if (segp == path)
          continue;

        /* "segp" is pointing to the end of a previous segment; find it's
         * start.  We need to back up to the previous segment and start
         * over with that to handle things like "foo/bar/../..".  If we
         * don't do this, then on the first pass we'll remove the "bar/..",
         * but be pointing at the second ".." so we won't realize we can also
         * remove the "foo/..".
         */
        cur = segp;
        while ((cur > path) && (cur[-1] != '/'))
          --cur;
    }
    out[0] = '\0';

    /*
     * g) If the resulting buffer string still begins with one or more
     *    complete path segments of "..", then the reference is
     *    considered to be in error. Implementations may handle this
     *    error by retaining these components in the resolved path (i.e.,
     *    treating them as part of the final URI), by removing them from
     *    the resolved path (i.e., discarding relative levels above the
     *    root), or by avoiding traversal of the reference.
     *
     * We discard them from the final path.
     */
    if (path[0] == '/') {
      cur = path;
      while ((cur[0] == '/') && (cur[1] == '.') && (cur[2] == '.')
             && ((cur[3] == '/') || (cur[3] == '\0')))
	cur += 3;

      if (cur != path) {
	out = path;
	while (cur[0] != '\0')
          (out++)[0] = (cur++)[0];
	out[0] = 0;
      }
    }

    return(0);
}

static int is_hex(char c) {
    if (((c >= '0') && (c <= '9')) ||
        ((c >= 'a') && (c <= 'f')) ||
        ((c >= 'A') && (c <= 'F')))
	return(1);
    return(0);
}


/**
 * uri_string_unescape:
 * @str:  the string to unescape
 * @len:   the length in bytes to unescape (or <= 0 to indicate full string)
 * @target:  optional destination buffer
 *
 * Unescaping routine, but does not check that the string is an URI. The
 * output is a direct unsigned char translation of %XX values (no encoding)
 * Note that the length of the result can only be smaller or same size as
 * the input string.
 *
 * Returns a copy of the string, but unescaped, will return NULL only in case
 * of error
 */
char *
uri_string_unescape(const char *str, int len, char *target) {
    char *ret, *out;
    const char *in;

    if (str == NULL)
	return(NULL);
    if (len <= 0) len = strlen(str);
    if (len < 0) return(NULL);

    if (target == NULL) {
	ret = g_malloc(len + 1);
    } else
	ret = target;
    in = str;
    out = ret;
    while(len > 0) {
	if ((len > 2) && (*in == '%') && (is_hex(in[1])) && (is_hex(in[2]))) {
	    in++;
	    if ((*in >= '0') && (*in <= '9'))
	        *out = (*in - '0');
	    else if ((*in >= 'a') && (*in <= 'f'))
	        *out = (*in - 'a') + 10;
	    else if ((*in >= 'A') && (*in <= 'F'))
	        *out = (*in - 'A') + 10;
	    in++;
	    if ((*in >= '0') && (*in <= '9'))
	        *out = *out * 16 + (*in - '0');
	    else if ((*in >= 'a') && (*in <= 'f'))
	        *out = *out * 16 + (*in - 'a') + 10;
	    else if ((*in >= 'A') && (*in <= 'F'))
	        *out = *out * 16 + (*in - 'A') + 10;
	    in++;
	    len -= 3;
	    out++;
	} else {
	    *out++ = *in++;
	    len--;
	}
    }
    *out = 0;
    return(ret);
}

/**
 * uri_string_escape:
 * @str:  string to escape
 * @list: exception list string of chars not to escape
 *
 * This routine escapes a string to hex, ignoring reserved characters (a-z)
 * and the characters in the exception list.
 *
 * Returns a new escaped string or NULL in case of error.
 */
char *
uri_string_escape(const char *str, const char *list) {
    char *ret, ch;
    char *temp;
    const char *in;
    int len, out;

    if (str == NULL)
	return(NULL);
    if (str[0] == 0)
	return(g_strdup(str));
    len = strlen(str);
    if (!(len > 0)) return(NULL);

    len += 20;
    ret = g_malloc(len);
    in = str;
    out = 0;
    while(*in != 0) {
	if (len - out <= 3) {
            temp = realloc2n(ret, &len);
	    ret = temp;
	}

	ch = *in;

	if ((ch != '@') && (!IS_UNRESERVED(ch)) && (!strchr(list, ch))) {
	    unsigned char val;
	    ret[out++] = '%';
	    val = ch >> 4;
	    if (val <= 9)
		ret[out++] = '0' + val;
	    else
		ret[out++] = 'A' + val - 0xA;
	    val = ch & 0xF;
	    if (val <= 9)
		ret[out++] = '0' + val;
	    else
		ret[out++] = 'A' + val - 0xA;
	    in++;
	} else {
	    ret[out++] = *in++;
	}

    }
    ret[out] = 0;
    return(ret);
}

/************************************************************************
 *									*
 *			Public functions				*
 *									*
 ************************************************************************/

/**
 * uri_resolve:
 * @URI:  the URI instance found in the document
 * @base:  the base value
 *
 * Computes he final URI of the reference done by checking that
 * the given URI is valid, and building the final URI using the
 * base URI. This is processed according to section 5.2 of the
 * RFC 2396
 *
 * 5.2. Resolving Relative References to Absolute Form
 *
 * Returns a new URI string (to be freed by the caller) or NULL in case
 *         of error.
 */
char *
uri_resolve(const char *uri, const char *base) {
    char *val = NULL;
    int ret, len, indx, cur, out;
    URI *ref = NULL;
    URI *bas = NULL;
    URI *res = NULL;

    /*
     * 1) The URI reference is parsed into the potential four components and
     *    fragment identifier, as described in Section 4.3.
     *
     *    NOTE that a completely empty URI is treated by modern browsers
     *    as a reference to "." rather than as a synonym for the current
     *    URI.  Should we do that here?
     */
    if (uri == NULL)
	ret = -1;
    else {
	if (*uri) {
	    ref = uri_new();
	    ret = uri_parse_into(ref, uri);
	}
	else
	    ret = 0;
    }
    if (ret != 0)
	goto done;
    if ((ref != NULL) && (ref->scheme != NULL)) {
	/*
	 * The URI is absolute don't modify.
	 */
	val = g_strdup(uri);
	goto done;
    }
    if (base == NULL)
	ret = -1;
    else {
	bas = uri_new();
	ret = uri_parse_into(bas, base);
    }
    if (ret != 0) {
	if (ref)
	    val = uri_to_string(ref);
	goto done;
    }
    if (ref == NULL) {
	/*
	 * the base fragment must be ignored
	 */
        g_free(bas->fragment);
        bas->fragment = NULL;
	val = uri_to_string(bas);
	goto done;
    }

    /*
     * 2) If the path component is empty and the scheme, authority, and
     *    query components are undefined, then it is a reference to the
     *    current document and we are done.  Otherwise, the reference URI's
     *    query and fragment components are defined as found (or not found)
     *    within the URI reference and not inherited from the base URI.
     *
     *    NOTE that in modern browsers, the parsing differs from the above
     *    in the following aspect:  the query component is allowed to be
     *    defined while still treating this as a reference to the current
     *    document.
     */
    res = uri_new();
    if ((ref->scheme == NULL) && (ref->path == NULL) &&
	((ref->authority == NULL) && (ref->server == NULL))) {
        res->scheme = g_strdup(bas->scheme);
	if (bas->authority != NULL)
	    res->authority = g_strdup(bas->authority);
	else if (bas->server != NULL) {
            res->server = g_strdup(bas->server);
            res->user = g_strdup(bas->user);
            res->port = bas->port;
	}
        res->path = g_strdup(bas->path);
        if (ref->query != NULL) {
	    res->query = g_strdup (ref->query);
        } else {
            res->query = g_strdup(bas->query);
        }
        res->fragment = g_strdup(ref->fragment);
	goto step_7;
    }

    /*
     * 3) If the scheme component is defined, indicating that the reference
     *    starts with a scheme name, then the reference is interpreted as an
     *    absolute URI and we are done.  Otherwise, the reference URI's
     *    scheme is inherited from the base URI's scheme component.
     */
    if (ref->scheme != NULL) {
	val = uri_to_string(ref);
	goto done;
    }
    res->scheme = g_strdup(bas->scheme);

    res->query = g_strdup(ref->query);
    res->fragment = g_strdup(ref->fragment);

    /*
     * 4) If the authority component is defined, then the reference is a
     *    network-path and we skip to step 7.  Otherwise, the reference
     *    URI's authority is inherited from the base URI's authority
     *    component, which will also be undefined if the URI scheme does not
     *    use an authority component.
     */
    if ((ref->authority != NULL) || (ref->server != NULL)) {
	if (ref->authority != NULL)
	    res->authority = g_strdup(ref->authority);
	else {
	    res->server = g_strdup(ref->server);
            res->user = g_strdup(ref->user);
            res->port = ref->port;
	}
        res->path = g_strdup(ref->path);
	goto step_7;
    }
    if (bas->authority != NULL)
	res->authority = g_strdup(bas->authority);
    else if (bas->server != NULL) {
        res->server = g_strdup(bas->server);
        res->user = g_strdup(bas->user);
	res->port = bas->port;
    }

    /*
     * 5) If the path component begins with a slash character ("/"), then
     *    the reference is an absolute-path and we skip to step 7.
     */
    if ((ref->path != NULL) && (ref->path[0] == '/')) {
	res->path = g_strdup(ref->path);
	goto step_7;
    }


    /*
     * 6) If this step is reached, then we are resolving a relative-path
     *    reference.  The relative path needs to be merged with the base
     *    URI's path.  Although there are many ways to do this, we will
     *    describe a simple method using a separate string buffer.
     *
     * Allocate a buffer large enough for the result string.
     */
    len = 2; /* extra / and 0 */
    if (ref->path != NULL)
	len += strlen(ref->path);
    if (bas->path != NULL)
	len += strlen(bas->path);
    res->path = g_malloc(len);
    res->path[0] = 0;

    /*
     * a) All but the last segment of the base URI's path component is
     *    copied to the buffer.  In other words, any characters after the
     *    last (right-most) slash character, if any, are excluded.
     */
    cur = 0;
    out = 0;
    if (bas->path != NULL) {
	while (bas->path[cur] != 0) {
	    while ((bas->path[cur] != 0) && (bas->path[cur] != '/'))
		cur++;
	    if (bas->path[cur] == 0)
		break;

	    cur++;
	    while (out < cur) {
		res->path[out] = bas->path[out];
		out++;
	    }
	}
    }
    res->path[out] = 0;

    /*
     * b) The reference's path component is appended to the buffer
     *    string.
     */
    if (ref->path != NULL && ref->path[0] != 0) {
	indx = 0;
	/*
	 * Ensure the path includes a '/'
	 */
	if ((out == 0) && (bas->server != NULL))
	    res->path[out++] = '/';
	while (ref->path[indx] != 0) {
	    res->path[out++] = ref->path[indx++];
	}
    }
    res->path[out] = 0;

    /*
     * Steps c) to h) are really path normalization steps
     */
    normalize_uri_path(res->path);

step_7:

    /*
     * 7) The resulting URI components, including any inherited from the
     *    base URI, are recombined to give the absolute form of the URI
     *    reference.
     */
    val = uri_to_string(res);

done:
    if (ref != NULL)
	uri_free(ref);
    if (bas != NULL)
	uri_free(bas);
    if (res != NULL)
	uri_free(res);
    return(val);
}

/**
 * uri_resolve_relative:
 * @URI:  the URI reference under consideration
 * @base:  the base value
 *
 * Expresses the URI of the reference in terms relative to the
 * base.  Some examples of this operation include:
 *     base = "http://site1.com/docs/book1.html"
 *        URI input                        URI returned
 *     docs/pic1.gif                    pic1.gif
 *     docs/img/pic1.gif                img/pic1.gif
 *     img/pic1.gif                     ../img/pic1.gif
 *     http://site1.com/docs/pic1.gif   pic1.gif
 *     http://site2.com/docs/pic1.gif   http://site2.com/docs/pic1.gif
 *
 *     base = "docs/book1.html"
 *        URI input                        URI returned
 *     docs/pic1.gif                    pic1.gif
 *     docs/img/pic1.gif                img/pic1.gif
 *     img/pic1.gif                     ../img/pic1.gif
 *     http://site1.com/docs/pic1.gif   http://site1.com/docs/pic1.gif
 *
 *
 * Note: if the URI reference is really weird or complicated, it may be
 *       worthwhile to first convert it into a "nice" one by calling
 *       uri_resolve (using 'base') before calling this routine,
 *       since this routine (for reasonable efficiency) assumes URI has
 *       already been through some validation.
 *
 * Returns a new URI string (to be freed by the caller) or NULL in case
 * error.
 */
char *
uri_resolve_relative (const char *uri, const char * base)
{
    char *val = NULL;
    int ret;
    int ix;
    int pos = 0;
    int nbslash = 0;
    int len;
    URI *ref = NULL;
    URI *bas = NULL;
    char *bptr, *uptr, *vptr;
    int remove_path = 0;

    if ((uri == NULL) || (*uri == 0))
	return NULL;

    /*
     * First parse URI into a standard form
     */
    ref = uri_new ();
    /* If URI not already in "relative" form */
    if (uri[0] != '.') {
	ret = uri_parse_into (ref, uri);
	if (ret != 0)
	    goto done;		/* Error in URI, return NULL */
    } else
	ref->path = g_strdup(uri);

    /*
     * Next parse base into the same standard form
     */
    if ((base == NULL) || (*base == 0)) {
	val = g_strdup (uri);
	goto done;
    }
    bas = uri_new ();
    if (base[0] != '.') {
	ret = uri_parse_into (bas, base);
	if (ret != 0)
	    goto done;		/* Error in base, return NULL */
    } else
	bas->path = g_strdup(base);

    /*
     * If the scheme / server on the URI differs from the base,
     * just return the URI
     */
    if ((ref->scheme != NULL) &&
	((bas->scheme == NULL) ||
	 (strcmp (bas->scheme, ref->scheme)) ||
	 (strcmp (bas->server, ref->server)))) {
	val = g_strdup (uri);
	goto done;
    }
    if (bas->path == ref->path ||
        (bas->path && ref->path && !strcmp(bas->path, ref->path))) {
	val = g_strdup("");
	goto done;
    }
    if (bas->path == NULL) {
	val = g_strdup(ref->path);
	goto done;
    }
    if (ref->path == NULL) {
        ref->path = (char *) "/";
	remove_path = 1;
    }

    /*
     * At this point (at last!) we can compare the two paths
     *
     * First we take care of the special case where either of the
     * two path components may be missing (bug 316224)
     */
    if (bas->path == NULL) {
	if (ref->path != NULL) {
	    uptr = ref->path;
	    if (*uptr == '/')
		uptr++;
	    /* exception characters from uri_to_string */
	    val = uri_string_escape(uptr, "/;&=+$,");
	}
	goto done;
    }
    bptr = bas->path;
    if (ref->path == NULL) {
	for (ix = 0; bptr[ix] != 0; ix++) {
	    if (bptr[ix] == '/')
		nbslash++;
	}
	uptr = NULL;
	len = 1;	/* this is for a string terminator only */
    } else {
    /*
     * Next we compare the two strings and find where they first differ
     */
	if ((ref->path[pos] == '.') && (ref->path[pos+1] == '/'))
            pos += 2;
	if ((*bptr == '.') && (bptr[1] == '/'))
            bptr += 2;
	else if ((*bptr == '/') && (ref->path[pos] != '/'))
	    bptr++;
	while ((bptr[pos] == ref->path[pos]) && (bptr[pos] != 0))
	    pos++;

	if (bptr[pos] == ref->path[pos]) {
	    val = g_strdup("");
	    goto done;		/* (I can't imagine why anyone would do this) */
	}

	/*
	 * In URI, "back up" to the last '/' encountered.  This will be the
	 * beginning of the "unique" suffix of URI
	 */
	ix = pos;
	if ((ref->path[ix] == '/') && (ix > 0))
	    ix--;
	else if ((ref->path[ix] == 0) && (ix > 1) && (ref->path[ix - 1] == '/'))
	    ix -= 2;
	for (; ix > 0; ix--) {
	    if (ref->path[ix] == '/')
		break;
	}
	if (ix == 0) {
	    uptr = ref->path;
	} else {
	    ix++;
	    uptr = &ref->path[ix];
	}

	/*
	 * In base, count the number of '/' from the differing point
	 */
	if (bptr[pos] != ref->path[pos]) {/* check for trivial URI == base */
	    for (; bptr[ix] != 0; ix++) {
		if (bptr[ix] == '/')
		    nbslash++;
	    }
	}
	len = strlen (uptr) + 1;
    }

    if (nbslash == 0) {
	if (uptr != NULL)
	    /* exception characters from uri_to_string */
	    val = uri_string_escape(uptr, "/;&=+$,");
	goto done;
    }

    /*
     * Allocate just enough space for the returned string -
     * length of the remainder of the URI, plus enough space
     * for the "../" groups, plus one for the terminator
     */
    val = g_malloc (len + 3 * nbslash);
    vptr = val;
    /*
     * Put in as many "../" as needed
     */
    for (; nbslash>0; nbslash--) {
	*vptr++ = '.';
	*vptr++ = '.';
	*vptr++ = '/';
    }
    /*
     * Finish up with the end of the URI
     */
    if (uptr != NULL) {
        if ((vptr > val) && (len > 0) &&
	    (uptr[0] == '/') && (vptr[-1] == '/')) {
	    memcpy (vptr, uptr + 1, len - 1);
	    vptr[len - 2] = 0;
	} else {
	    memcpy (vptr, uptr, len);
	    vptr[len - 1] = 0;
	}
    } else {
	vptr[len - 1] = 0;
    }

    /* escape the freshly-built path */
    vptr = val;
	/* exception characters from uri_to_string */
    val = uri_string_escape(vptr, "/;&=+$,");
    g_free(vptr);

done:
    /*
     * Free the working variables
     */
    if (remove_path != 0)
        ref->path = NULL;
    if (ref != NULL)
	uri_free (ref);
    if (bas != NULL)
	uri_free (bas);

    return val;
}

/*
 * Utility functions to help parse and assemble query strings.
 */

struct QueryParams *
query_params_new (int init_alloc)
{
    struct QueryParams *ps;

    if (init_alloc <= 0) init_alloc = 1;

    ps = g_new(QueryParams, 1);
    ps->n = 0;
    ps->alloc = init_alloc;
    ps->p = g_new(QueryParam, ps->alloc);

    return ps;
}

/* Ensure there is space to store at least one more parameter
 * at the end of the set.
 */
static int
query_params_append (struct QueryParams *ps,
               const char *name, const char *value)
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

void
query_params_free (struct QueryParams *ps)
{
    int i;

    for (i = 0; i < ps->n; ++i) {
        g_free (ps->p[i].name);
        g_free (ps->p[i].value);
    }
    g_free (ps->p);
    g_free (ps);
}

struct QueryParams *
query_params_parse (const char *query)
{
    struct QueryParams *ps;
    const char *end, *eq;

    ps = query_params_new (0);
    if (!query || query[0] == '\0') return ps;

    while (*query) {
        char *name = NULL, *value = NULL;

        /* Find the next separator, or end of the string. */
        end = strchr (query, '&');
        if (!end)
            end = strchr (query, ';');
        if (!end)
            end = query + strlen (query);

        /* Find the first '=' character between here and end. */
        eq = strchr (query, '=');
        if (eq && eq >= end) eq = NULL;

        /* Empty section (eg. "&&"). */
        if (end == query)
            goto next;

        /* If there is no '=' character, then we have just "name"
         * and consistent with CGI.pm we assume value is "".
         */
        else if (!eq) {
            name = uri_string_unescape (query, end - query, NULL);
            value = NULL;
        }
        /* Or if we have "name=" here (works around annoying
         * problem when calling uri_string_unescape with len = 0).
         */
        else if (eq+1 == end) {
            name = uri_string_unescape (query, eq - query, NULL);
            value = g_new0(char, 1);
        }
        /* If the '=' character is at the beginning then we have
         * "=value" and consistent with CGI.pm we _ignore_ this.
         */
        else if (query == eq)
            goto next;

        /* Otherwise it's "name=value". */
        else {
            name = uri_string_unescape (query, eq - query, NULL);
            value = uri_string_unescape (eq+1, end - (eq+1), NULL);
        }

        /* Append to the parameter set. */
        query_params_append (ps, name, value);
        g_free(name);
        g_free(value);

    next:
        query = end;
        if (*query) query ++; /* skip '&' separator */
    }

    return ps;
}
