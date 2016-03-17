/*
 * QEMU Crypto random number provider
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

int qcrypto_random_bytes(uint8_t *buf G_GNUC_UNUSED,
                         size_t buflen G_GNUC_UNUSED,
                         Error **errp)
{
    error_setg(errp, "No random byte source provided in this build");
    return -1;
}
