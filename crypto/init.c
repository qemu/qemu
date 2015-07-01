/*
 * QEMU Crypto initialization
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

#include "crypto/init.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

/* #define DEBUG_GNUTLS */

#ifdef DEBUG_GNUTLS
static void qcrypto_gnutls_log(int level, const char *str)
{
    fprintf(stderr, "%d: %s", level, str);
}
#endif

int qcrypto_init(Error **errp)
{
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
    return 0;
}

#else /* ! CONFIG_GNUTLS */

int qcrypto_init(Error **errp G_GNUC_UNUSED)
{
    return 0;
}

#endif /* ! CONFIG_GNUTLS */
