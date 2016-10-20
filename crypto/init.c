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

/* #define DEBUG_GNUTLS */

/*
 * If GNUTLS is built against GCrypt then
 *
 *  - When GNUTLS >= 2.12, we must not initialize gcrypt threading
 *    because GNUTLS will do that itself
 *  - When GNUTLS < 2.12 we must always initialize gcrypt threading
 *  - When GNUTLS is disabled we must always initialize gcrypt threading
 *
 * But....
 *
 *    When gcrypt >= 1.6.0 we must not initialize gcrypt threading
 *    because gcrypt will do that itself.
 *
 * So we need to init gcrypt threading if
 *
 *   - gcrypt < 1.6.0
 * AND
 *      - gnutls < 2.12
 *   OR
 *      - gnutls is disabled
 *
 */

#if (defined(CONFIG_GCRYPT) &&                  \
     (!defined(CONFIG_GNUTLS) ||                \
     (LIBGNUTLS_VERSION_NUMBER < 0x020c00)) &&    \
     (!defined(GCRYPT_VERSION_NUMBER) ||        \
      (GCRYPT_VERSION_NUMBER < 0x010600)))
#define QCRYPTO_INIT_GCRYPT_THREADS
#else
#undef QCRYPTO_INIT_GCRYPT_THREADS
#endif

#ifdef DEBUG_GNUTLS
static void qcrypto_gnutls_log(int level, const char *str)
{
    fprintf(stderr, "%d: %s", level, str);
}
#endif

#ifdef QCRYPTO_INIT_GCRYPT_THREADS
static int qcrypto_gcrypt_mutex_init(void **priv)
{                                                                             \
    QemuMutex *lock = NULL;
    lock = g_new0(QemuMutex, 1);
    qemu_mutex_init(lock);
    *priv = lock;
    return 0;
}

static int qcrypto_gcrypt_mutex_destroy(void **priv)
{
    QemuMutex *lock = *priv;
    qemu_mutex_destroy(lock);
    g_free(lock);
    return 0;
}

static int qcrypto_gcrypt_mutex_lock(void **priv)
{
    QemuMutex *lock = *priv;
    qemu_mutex_lock(lock);
    return 0;
}

static int qcrypto_gcrypt_mutex_unlock(void **priv)
{
    QemuMutex *lock = *priv;
    qemu_mutex_unlock(lock);
    return 0;
}

static struct gcry_thread_cbs qcrypto_gcrypt_thread_impl = {
    (GCRY_THREAD_OPTION_PTHREAD | (GCRY_THREAD_OPTION_VERSION << 8)),
    NULL,
    qcrypto_gcrypt_mutex_init,
    qcrypto_gcrypt_mutex_destroy,
    qcrypto_gcrypt_mutex_lock,
    qcrypto_gcrypt_mutex_unlock,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
#endif /* QCRYPTO_INIT_GCRYPT */

int qcrypto_init(Error **errp)
{
#ifdef QCRYPTO_INIT_GCRYPT_THREADS
    gcry_control(GCRYCTL_SET_THREAD_CBS, &qcrypto_gcrypt_thread_impl);
#endif /* QCRYPTO_INIT_GCRYPT_THREADS */

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
    if (!gcry_check_version(GCRYPT_VERSION)) {
        error_setg(errp, "Unable to initialize gcrypt");
        return -1;
    }
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
#endif

    return 0;
}
