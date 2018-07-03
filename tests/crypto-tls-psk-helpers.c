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

/* Include this first because it defines QCRYPTO_HAVE_TLS_TEST_SUPPORT */
#include "crypto-tls-x509-helpers.h"

#include "crypto-tls-psk-helpers.h"
#include "qemu/sockets.h"

#ifdef QCRYPTO_HAVE_TLS_TEST_SUPPORT

void test_tls_psk_init(const char *pskfile)
{
    FILE *fp;

    fp = fopen(pskfile, "w");
    if (fp == NULL) {
        g_critical("Failed to create pskfile %s", pskfile);
        abort();
    }
    /* Don't do this in real applications!  Use psktool. */
    fprintf(fp, "qemu:009d5638c40fde0c\n");
    fclose(fp);
}

void test_tls_psk_cleanup(const char *pskfile)
{
    unlink(pskfile);
}

#endif /* QCRYPTO_HAVE_TLS_TEST_SUPPORT */
