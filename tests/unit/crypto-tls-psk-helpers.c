/*
 * Copyright (C) 2015-2018 Red Hat, Inc.
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Richard W.M. Jones <rjones@redhat.com>
 */

#include "qemu/osdep.h"

#include "crypto-tls-psk-helpers.h"
#include "qemu/sockets.h"

static void
test_tls_psk_init_common(const char *pskfile, const char *user, const char *key)
{
    g_autoptr(GError) gerr = NULL;
    g_autofree char *line = g_strdup_printf("%s:%s\n", user, key);

    g_file_set_contents(pskfile, line, strlen(line), &gerr);
    if (gerr != NULL) {
        g_critical("Failed to create pskfile %s: %s", pskfile, gerr->message);
        abort();
    }
}

void test_tls_psk_init(const char *pskfile)
{
    /* Don't hard code a key like this in real applications!  Use psktool. */
    test_tls_psk_init_common(pskfile, "qemu", "009d5638c40fde0c");
}

void test_tls_psk_init_alt(const char *pskfile)
{
    /* Don't hard code a key like this in real applications!  Use psktool. */
    test_tls_psk_init_common(pskfile, "qemu", "10ffa6a2c42f0388");
}

void test_tls_psk_cleanup(const char *pskfile)
{
    unlink(pskfile);
}
