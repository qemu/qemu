/*
 * QEMU Crypto "none" random number provider
 *
 * Copyright (c) 2020 Marek Marczykowski-Górecki
 *                      <marmarek@invisiblethingslab.com>
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
#include "qapi/error.h"

int qcrypto_random_init(Error **errp)
{
    return 0;
}

int qcrypto_random_bytes(void *buf,
                         size_t buflen,
                         Error **errp)
{
    error_setg(errp, "Random bytes not available with \"none\" rng");
    return -1;
}
