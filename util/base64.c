/*
 * QEMU base64 helpers
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
#include <config-host.h>

#include "qemu/base64.h"

static const char *base64_valid_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n";

uint8_t *qbase64_decode(const char *input,
                        size_t in_len,
                        size_t *out_len,
                        Error **errp)
{
    *out_len = 0;

    if (in_len != -1) {
        /* Lack of NUL terminator is an error */
        if (input[in_len] != '\0') {
            error_setg(errp, "Base64 data is not NUL terminated");
            return NULL;
        }
        /* Check there's no NULs embedded since we expect
         * this to be valid base64 data */
        if (memchr(input, '\0', in_len) != NULL) {
            error_setg(errp, "Base64 data contains embedded NUL characters");
            return NULL;
        }

        /* Now we know its a valid nul terminated string
         * strspn is safe to use... */
    } else {
        in_len = strlen(input);
    }

    if (strspn(input, base64_valid_chars) != in_len) {
        error_setg(errp, "Base64 data contains invalid characters");
        return NULL;
    }

    return g_base64_decode(input, out_len);
}
