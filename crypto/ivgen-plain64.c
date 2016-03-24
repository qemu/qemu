/*
 * QEMU Crypto block IV generator - plain
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
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "crypto/ivgen-plain.h"

static int qcrypto_ivgen_plain_init(QCryptoIVGen *ivgen,
                                    const uint8_t *key, size_t nkey,
                                    Error **errp)
{
    return 0;
}

static int qcrypto_ivgen_plain_calculate(QCryptoIVGen *ivgen,
                                         uint64_t sector,
                                         uint8_t *iv, size_t niv,
                                         Error **errp)
{
    size_t ivprefix;
    ivprefix = sizeof(sector);
    sector = cpu_to_le64(sector);
    if (ivprefix > niv) {
        ivprefix = niv;
    }
    memcpy(iv, &sector, ivprefix);
    if (ivprefix < niv) {
        memset(iv + ivprefix, 0, niv - ivprefix);
    }
    return 0;
}

static void qcrypto_ivgen_plain_cleanup(QCryptoIVGen *ivgen)
{
}


struct QCryptoIVGenDriver qcrypto_ivgen_plain64 = {
    .init = qcrypto_ivgen_plain_init,
    .calculate = qcrypto_ivgen_plain_calculate,
    .cleanup = qcrypto_ivgen_plain_cleanup,
};

