/**
 * Summary: library of generic URI related routines
 * Description: library of generic URI related routines
 *              Implements RFC 2396
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
 * Author: Daniel Veillard
 **
 * Copyright (C) 2007 Red Hat, Inc.
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
 * Utility functions to help parse and assemble query strings.
 */

#ifndef QEMU_URI_H
#define QEMU_URI_H

/**
 * URI:
 *
 * A parsed URI reference. This is a struct containing the various fields
 * as described in RFC 2396 but separated for further processing.
 */
typedef struct URI {
    char *scheme;	/* the URI scheme */
    char *opaque;	/* opaque part */
    char *authority;	/* the authority part */
    char *server;	/* the server part */
    char *user;		/* the user part */
    int port;		/* the port number */
    char *path;		/* the path string */
    char *fragment;	/* the fragment identifier */
    int  cleanup;	/* parsing potentially unclean URI */
    char *query;	/* the query string (as it appears in the URI) */
} URI;

URI *uri_new(void);
char *uri_resolve(const char *URI, const char *base);
char *uri_resolve_relative(const char *URI, const char *base);
URI *uri_parse(const char *str);
URI *uri_parse_raw(const char *str, int raw);
int uri_parse_into(URI *uri, const char *str);
char *uri_to_string(URI *uri);
char *uri_string_escape(const char *str, const char *list);
char *uri_string_unescape(const char *str, int len, char *target);
void uri_free(URI *uri);

/* Single web service query parameter 'name=value'. */
typedef struct QueryParam {
  char *name;			/* Name (unescaped). */
  char *value;			/* Value (unescaped). */
  int ignore;			/* Ignore this field in qparam_get_query */
} QueryParam;

/* Set of parameters. */
typedef struct QueryParams {
  int n;			/* number of parameters used */
  int alloc;			/* allocated space */
  QueryParam *p;		/* array of parameters */
} QueryParams;

struct QueryParams *query_params_new (int init_alloc);
extern QueryParams *query_params_parse (const char *query);
extern void query_params_free (QueryParams *ps);

#endif /* QEMU_URI_H */
