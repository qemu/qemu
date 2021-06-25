/*
 * QEMU Crypto initialization
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
#include "crypto/init.h"
#include "qapi/error.h"
#include "qemu/thread.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#endif

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>
#endif

#include "crypto/random.h"

/* #define DEBUG_GNUTLS */
#ifdef DEBUG_GNUTLS
static void qcrypto_gnutls_log(int level, const char *str)
{
    fprintf(stderr, "%d: %s", level, str);
}
#endif

int qcrypto_init(Error **errp)
{
#ifdef CONFIG_GNUTLS
    int ret;
    ret = gnutls_global_init();
    if (ret < 0) {
        error_setg(errp,
                   "Unable to initialize GNUTLS library: %s",
                   gnutls_strerror(ret));
        return -1;
    }
#ifdef DEBUG_GNUTLS
    gnutls_global_set_log_level(10);
    gnutls_global_set_log_function(qcrypto_gnutls_log);
#endif
#endif

#ifdef CONFIG_GCRYPT
    if (!gcry_check_version(NULL)) {
        error_setg(errp, "Unable to initialize gcrypt");
        return -1;
    }
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
#endif

    if (qcrypto_random_init(errp) < 0) {
        return -1;
    }

    return 0;
}
