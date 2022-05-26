/*
 * QEMU Crypto RSA key parser
 *
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
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

#include "rsakey.h"

void qcrypto_akcipher_rsakey_free(QCryptoAkCipherRSAKey *rsa_key)
{
    if (!rsa_key) {
        return;
    }
    g_free(rsa_key->n.data);
    g_free(rsa_key->e.data);
    g_free(rsa_key->d.data);
    g_free(rsa_key->p.data);
    g_free(rsa_key->q.data);
    g_free(rsa_key->dp.data);
    g_free(rsa_key->dq.data);
    g_free(rsa_key->u.data);
    g_free(rsa_key);
}

#if defined(CONFIG_NETTLE) && defined(CONFIG_HOGWEED)
#include "rsakey-nettle.c.inc"
#else
#include "rsakey-builtin.c.inc"
#endif
