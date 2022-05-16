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

#ifndef TESTS_CRYPTO_TLS_PSK_HELPERS_H
#define TESTS_CRYPTO_TLS_PSK_HELPERS_H

#include <gnutls/gnutls.h>

void test_tls_psk_init(const char *keyfile);
void test_tls_psk_init_alt(const char *keyfile);
void test_tls_psk_cleanup(const char *keyfile);

#endif
