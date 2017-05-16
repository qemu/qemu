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

#ifdef _WIN32
#include <wincrypt.h>
static HCRYPTPROV hCryptProv;
#else
static int fd; /* a file handle to either /dev/urandom or /dev/random */
#endif

int qcrypto_random_init(Error **errp)
{
#ifndef _WIN32
    /* TBD perhaps also add support for BSD getentropy / Linux
     * getrandom syscalls directly */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1 && errno == ENOENT) {
        fd = open("/dev/random", O_RDONLY);
    }

    if (fd < 0) {
        error_setg(errp, "No /dev/urandom or /dev/random found");
        return -1;
    }
#else
    if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL,
                             CRYPT_SILENT | CRYPT_VERIFYCONTEXT)) {
        error_setg_win32(errp, GetLastError(),
                         "Unable to create cryptographic provider");
        return -1;
    }
#endif

    return 0;
}

int qcrypto_random_bytes(uint8_t *buf G_GNUC_UNUSED,
                         size_t buflen G_GNUC_UNUSED,
                         Error **errp)
{
#ifndef _WIN32
    int ret = -1;
    int got;

    while (buflen > 0) {
        got = read(fd, buf, buflen);
        if (got < 0) {
            error_setg_errno(errp, errno,
                             "Unable to read random bytes");
            goto cleanup;
        } else if (!got) {
            error_setg(errp,
                       "Unexpected EOF reading random bytes");
            goto cleanup;
        }
        buflen -= got;
        buf += got;
    }

    ret = 0;
 cleanup:
    return ret;
#else
    if (!CryptGenRandom(hCryptProv, buflen, buf)) {
        error_setg_win32(errp, GetLastError(),
                         "Unable to read random bytes");
        return -1;
    }

    return 0;
#endif
}
