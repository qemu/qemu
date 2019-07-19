/*
 * QEMU Crypto random number provider
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"

#include "crypto/random.h"

#include <gcrypt.h>

int qcrypto_random_bytes(void *buf,
                         size_t buflen,
                         Error **errp G_GNUC_UNUSED)
{
    gcry_randomize(buf, buflen, GCRY_STRONG_RANDOM);
    return 0;
}

int qcrypto_random_init(Error **errp G_GNUC_UNUSED) { return 0; }
