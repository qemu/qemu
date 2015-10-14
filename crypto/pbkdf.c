/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
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

#include "crypto/pbkdf.h"
#include <sys/resource.h>

#if defined CONFIG_NETTLE
#include "crypto/pbkdf-nettle.c"
#elif defined CONFIG_GCRYPT
#include "crypto/pbkdf-gcrypt.c"
#else /* ! CONFIG_GCRYPT */
#include "crypto/pbkdf-stub.c"
#endif /* ! CONFIG_GCRYPT */


int qcrypto_pbkdf2_count_iters(QCryptoHashAlgorithm hash,
                               const uint8_t *key, size_t nkey,
                               const uint8_t *salt, size_t nsalt,
                               Error **errp)
{
    uint8_t out[32];
    int iterations = (1 << 15);
    struct rusage start, end;
    unsigned long long delta;

    while (1) {
        if (getrusage(RUSAGE_THREAD, &start) < 0) {
            error_setg_errno(errp, errno, "Unable to get thread CPU usage");
            return -1;
        }
        if (qcrypto_pbkdf2(hash,
                           key, nkey,
                           salt, nsalt,
                           iterations,
                           out, sizeof(out),
                           errp) < 0) {
            return -1;
        }
        if (getrusage(RUSAGE_THREAD, &end) < 0) {
            error_setg_errno(errp, errno, "Unable to get thread CPU usage");
            return -1;
        }

        delta = (((end.ru_utime.tv_sec * 1000ll) +
                  (end.ru_utime.tv_usec / 1000)) -
                 ((start.ru_utime.tv_sec * 1000ll) +
                  (start.ru_utime.tv_usec / 1000)));

        if (delta > 500) {
            break;
        } else if (delta < 100) {
            iterations = iterations * 10;
        } else {
            iterations = (iterations * 1000 / delta);
        }
    }

    return iterations * 1000 / delta;
}
