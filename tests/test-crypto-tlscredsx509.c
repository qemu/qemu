/*
 * Copyright (C) 2015 Red Hat, Inc.
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include "qemu/osdep.h"

#include "crypto-tls-x509-helpers.h"
#include "crypto/tlscredsx509.h"
#include "qapi/error.h"

#ifdef QCRYPTO_HAVE_TLS_TEST_SUPPORT

#define WORKDIR "tests/test-crypto-tlscredsx509-work/"
#define KEYFILE WORKDIR "key-ctx.pem"

struct QCryptoTLSCredsTestData {
    bool isServer;
    const char *cacrt;
    const char *crt;
    bool expectFail;
};


static QCryptoTLSCreds *test_tls_creds_create(QCryptoTLSCredsEndpoint endpoint,
                                              const char *certdir,
                                              Error **errp)
{
    Object *parent = object_get_objects_root();
    Object *creds = object_new_with_props(
        TYPE_QCRYPTO_TLS_CREDS_X509,
        parent,
        "testtlscreds",
        errp,
        "endpoint", (endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER ?
                     "server" : "client"),
        "dir", certdir,
        "verify-peer", "yes",
        "sanity-check", "yes",
        NULL);

    if (*errp) {
        return NULL;
    }
    return QCRYPTO_TLS_CREDS(creds);
}

/*
 * This tests sanity checking of our own certificates
 *
 * The code being tested is used when TLS creds are created,
 * and aim to ensure QMEU has been configured with sane
 * certificates. This allows us to give much much much
 * clearer error messages to the admin when they misconfigure
 * things.
 */
static void test_tls_creds(const void *opaque)
{
    struct QCryptoTLSCredsTestData *data =
        (struct QCryptoTLSCredsTestData *)opaque;
    QCryptoTLSCreds *creds;
    Error *err = NULL;

#define CERT_DIR "tests/test-crypto-tlscredsx509-certs/"
    mkdir(CERT_DIR, 0700);

    unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    if (data->isServer) {
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT);
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY);
    } else {
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT);
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY);
    }

    if (access(data->cacrt, R_OK) == 0) {
        g_assert(link(data->cacrt,
                      CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT) == 0);
    }
    if (data->isServer) {
        if (access(data->crt, R_OK) == 0) {
            g_assert(link(data->crt,
                          CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT) == 0);
        }
        g_assert(link(KEYFILE,
                      CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY) == 0);
    } else {
        if (access(data->crt, R_OK) == 0) {
            g_assert(link(data->crt,
                          CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT) == 0);
        }
        g_assert(link(KEYFILE,
                      CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY) == 0);
    }

    creds = test_tls_creds_create(
        (data->isServer ?
         QCRYPTO_TLS_CREDS_ENDPOINT_SERVER :
         QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT),
        CERT_DIR,
        &err);

    if (data->expectFail) {
        error_free(err);
        g_assert(creds == NULL);
    } else {
        if (err) {
            g_printerr("Failed to generate creds: %s\n",
                       error_get_pretty(err));
            error_free(err);
        }
        g_assert(creds != NULL);
    }

    unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    if (data->isServer) {
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT);
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY);
    } else {
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT);
        unlink(CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY);
    }
    rmdir(CERT_DIR);
    if (creds) {
        object_unparent(OBJECT(creds));
    }
}

int main(int argc, char **argv)
{
    int ret;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    setenv("GNUTLS_FORCE_FIPS_MODE", "2", 1);

    mkdir(WORKDIR, 0700);

    test_tls_init(KEYFILE);

# define TLS_TEST_REG(name, isServer, caCrt, crt, expectFail)           \
    struct QCryptoTLSCredsTestData name = {                             \
        isServer, caCrt, crt, expectFail                                \
    };                                                                  \
    g_test_add_data_func("/qcrypto/tlscredsx509/" # name,               \
                         &name, test_tls_creds);                        \

    /* A perfect CA, perfect client & perfect server */

    /* Basic:CA:critical */
    TLS_ROOT_REQ(cacertreq,
                 "UK", "qemu CA", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);

    TLS_CERT_REQ(servercertreq, cacertreq,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    TLS_CERT_REQ(clientcertreq, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);

    TLS_TEST_REG(perfectserver, true,
                 cacertreq.filename, servercertreq.filename, false);
    TLS_TEST_REG(perfectclient, false,
                 cacertreq.filename, clientcertreq.filename, false);


    /* Some other CAs which are good */

    /* Basic:CA:critical */
    TLS_ROOT_REQ(cacert1req,
                 "UK", "qemu CA 1", NULL, NULL, NULL, NULL,
                 true, true, true,
                 false, false, 0,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert1req, cacert1req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);

    /* Basic:CA:not-critical */
    TLS_ROOT_REQ(cacert2req,
                 "UK", "qemu CA 2", NULL, NULL, NULL, NULL,
                 true, false, true,
                 false, false, 0,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert2req, cacert2req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);

    /* Key usage:cert-sign:critical */
    TLS_ROOT_REQ(cacert3req,
                 "UK", "qemu CA 3", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert3req, cacert3req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);

    TLS_TEST_REG(goodca1, true,
                 cacert1req.filename, servercert1req.filename, false);
    TLS_TEST_REG(goodca2, true,
                 cacert2req.filename, servercert2req.filename, false);
    TLS_TEST_REG(goodca3, true,
                 cacert3req.filename, servercert3req.filename, false);

    /* Now some bad certs */

    /* Key usage:dig-sig:not-critical */
    TLS_ROOT_REQ(cacert4req,
                 "UK", "qemu CA 4", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, false, GNUTLS_KEY_DIGITAL_SIGNATURE,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert4req, cacert4req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* no-basic */
    TLS_ROOT_REQ(cacert5req,
                 "UK", "qemu CA 5", NULL, NULL, NULL, NULL,
                 false, false, false,
                 false, false, 0,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert5req, cacert5req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* Key usage:dig-sig:critical */
    TLS_ROOT_REQ(cacert6req,
                 "UK", "qemu CA 6", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_DIGITAL_SIGNATURE,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercert6req, cacert6req,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);

    /* Technically a CA cert with basic constraints
     * key purpose == key signing + non-critical should
     * be rejected. GNUTLS < 3.1 does not reject it and
     * we don't anticipate them changing this behaviour
     */
    TLS_TEST_REG(badca1, true, cacert4req.filename, servercert4req.filename,
                (GNUTLS_VERSION_MAJOR == 3 && GNUTLS_VERSION_MINOR >= 1) ||
                GNUTLS_VERSION_MAJOR > 3);
    TLS_TEST_REG(badca2, true,
                 cacert5req.filename, servercert5req.filename, true);
    TLS_TEST_REG(badca3, true,
                 cacert6req.filename, servercert6req.filename, true);


    /* Various good servers */
    /* no usage or purpose */
    TLS_CERT_REQ(servercert7req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 false, false, NULL, NULL,
                 0, 0);
    /* usage:cert-sign+dig-sig+encipher:critical */
    TLS_CERT_REQ(servercert8req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT |
                 GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* usage:cert-sign:not-critical */
    TLS_CERT_REQ(servercert9req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, false, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* purpose:server:critical */
    TLS_CERT_REQ(servercert10req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* purpose:server:not-critical */
    TLS_CERT_REQ(servercert11req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, false, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* purpose:client+server:critical */
    TLS_CERT_REQ(servercert12req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true,
                 GNUTLS_KP_TLS_WWW_CLIENT, GNUTLS_KP_TLS_WWW_SERVER,
                 0, 0);
    /* purpose:client+server:not-critical */
    TLS_CERT_REQ(servercert13req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, false,
                 GNUTLS_KP_TLS_WWW_CLIENT, GNUTLS_KP_TLS_WWW_SERVER,
                 0, 0);

    TLS_TEST_REG(goodserver1, true,
                 cacertreq.filename, servercert7req.filename, false);
    TLS_TEST_REG(goodserver2, true,
                 cacertreq.filename, servercert8req.filename, false);
    TLS_TEST_REG(goodserver3, true,
                 cacertreq.filename, servercert9req.filename, false);
    TLS_TEST_REG(goodserver4, true,
                 cacertreq.filename, servercert10req.filename, false);
    TLS_TEST_REG(goodserver5, true,
                 cacertreq.filename, servercert11req.filename, false);
    TLS_TEST_REG(goodserver6, true,
                 cacertreq.filename, servercert12req.filename, false);
    TLS_TEST_REG(goodserver7, true,
                 cacertreq.filename, servercert13req.filename, false);

    /* Bad servers */

    /* usage:cert-sign:critical */
    TLS_CERT_REQ(servercert14req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* purpose:client:critical */
    TLS_CERT_REQ(servercert15req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);
    /* usage: none:critical */
    TLS_CERT_REQ(servercert16req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true, 0,
                 false, false, NULL, NULL,
                 0, 0);

    TLS_TEST_REG(badserver1, true,
                 cacertreq.filename, servercert14req.filename, true);
    TLS_TEST_REG(badserver2, true,
                 cacertreq.filename, servercert15req.filename, true);
    TLS_TEST_REG(badserver3, true,
                 cacertreq.filename, servercert16req.filename, true);



    /* Various good clients */
    /* no usage or purpose */
    TLS_CERT_REQ(clientcert1req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 false, false, NULL, NULL,
                 0, 0);
    /* usage:cert-sign+dig-sig+encipher:critical */
    TLS_CERT_REQ(clientcert2req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT |
                 GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* usage:cert-sign:not-critical */
    TLS_CERT_REQ(clientcert3req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, false, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* purpose:client:critical */
    TLS_CERT_REQ(clientcert4req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);
    /* purpose:client:not-critical */
    TLS_CERT_REQ(clientcert5req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, false, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);
    /* purpose:client+client:critical */
    TLS_CERT_REQ(clientcert6req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true,
                 GNUTLS_KP_TLS_WWW_CLIENT, GNUTLS_KP_TLS_WWW_SERVER,
                 0, 0);
    /* purpose:client+client:not-critical */
    TLS_CERT_REQ(clientcert7req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, false,
                 GNUTLS_KP_TLS_WWW_CLIENT, GNUTLS_KP_TLS_WWW_SERVER,
                 0, 0);

    TLS_TEST_REG(goodclient1, false,
                 cacertreq.filename, clientcert1req.filename, false);
    TLS_TEST_REG(goodclient2, false,
                 cacertreq.filename, clientcert2req.filename, false);
    TLS_TEST_REG(goodclient3, false,
                 cacertreq.filename, clientcert3req.filename, false);
    TLS_TEST_REG(goodclient4, false,
                 cacertreq.filename, clientcert4req.filename, false);
    TLS_TEST_REG(goodclient5, false,
                 cacertreq.filename, clientcert5req.filename, false);
    TLS_TEST_REG(goodclient6, false,
                 cacertreq.filename, clientcert6req.filename, false);
    TLS_TEST_REG(goodclient7, false,
                 cacertreq.filename, clientcert7req.filename, false);

    /* Bad clients */

    /* usage:cert-sign:critical */
    TLS_CERT_REQ(clientcert8req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    /* purpose:client:critical */
    TLS_CERT_REQ(clientcert9req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 false, false, 0,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* usage: none:critical */
    TLS_CERT_REQ(clientcert10req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true, 0,
                 false, false, NULL, NULL,
                 0, 0);

    TLS_TEST_REG(badclient1, false,
                 cacertreq.filename, clientcert8req.filename, true);
    TLS_TEST_REG(badclient2, false,
                 cacertreq.filename, clientcert9req.filename, true);
    TLS_TEST_REG(badclient3, false,
                 cacertreq.filename, clientcert10req.filename, true);



    /* Expired stuff */

    TLS_ROOT_REQ(cacertexpreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, -1);
    TLS_CERT_REQ(servercertexpreq, cacertexpreq,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    TLS_CERT_REQ(servercertexp1req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, -1);
    TLS_CERT_REQ(clientcertexp1req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, -1);

    TLS_TEST_REG(expired1, true,
                 cacertexpreq.filename, servercertexpreq.filename, true);
    TLS_TEST_REG(expired2, true,
                 cacertreq.filename, servercertexp1req.filename, true);
    TLS_TEST_REG(expired3, false,
                 cacertreq.filename, clientcertexp1req.filename, true);


    /* Not activated stuff */

    TLS_ROOT_REQ(cacertnewreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 1, 2);
    TLS_CERT_REQ(servercertnewreq, cacertnewreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    TLS_CERT_REQ(servercertnew1req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 1, 2);
    TLS_CERT_REQ(clientcertnew1req, cacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 1, 2);

    TLS_TEST_REG(inactive1, true,
                 cacertnewreq.filename, servercertnewreq.filename, true);
    TLS_TEST_REG(inactive2, true,
                 cacertreq.filename, servercertnew1req.filename, true);
    TLS_TEST_REG(inactive3, false,
                 cacertreq.filename, clientcertnew1req.filename, true);

    TLS_ROOT_REQ(cacertrootreq,
                 "UK", "qemu root", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(cacertlevel1areq, cacertrootreq,
                 "UK", "qemu level 1a", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(cacertlevel1breq, cacertrootreq,
                 "UK", "qemu level 1b", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(cacertlevel2areq, cacertlevel1areq,
                 "UK", "qemu level 2a", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);
    TLS_CERT_REQ(servercertlevel3areq, cacertlevel2areq,
                 "UK", "qemu.org", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    TLS_CERT_REQ(clientcertlevel2breq, cacertlevel1breq,
                 "UK", "qemu client level 2b", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);

    gnutls_x509_crt_t certchain[] = {
        cacertrootreq.crt,
        cacertlevel1areq.crt,
        cacertlevel1breq.crt,
        cacertlevel2areq.crt,
    };

    test_tls_write_cert_chain(WORKDIR "cacertchain-ctx.pem",
                              certchain,
                              G_N_ELEMENTS(certchain));

    TLS_TEST_REG(chain1, true,
                 WORKDIR "cacertchain-ctx.pem",
                 servercertlevel3areq.filename, false);
    TLS_TEST_REG(chain2, false,
                 WORKDIR "cacertchain-ctx.pem",
                 clientcertlevel2breq.filename, false);

    /* Some missing certs - first two are fatal, the last
     * is ok
     */
    TLS_TEST_REG(missingca, true,
                 "cacertdoesnotexist.pem",
                 servercert1req.filename, true);
    TLS_TEST_REG(missingserver, true,
                 cacert1req.filename,
                 "servercertdoesnotexist.pem", true);
    TLS_TEST_REG(missingclient, false,
                 cacert1req.filename,
                 "clientcertdoesnotexist.pem", false);

    ret = g_test_run();

    test_tls_discard_cert(&cacertreq);
    test_tls_discard_cert(&cacert1req);
    test_tls_discard_cert(&cacert2req);
    test_tls_discard_cert(&cacert3req);
    test_tls_discard_cert(&cacert4req);
    test_tls_discard_cert(&cacert5req);
    test_tls_discard_cert(&cacert6req);

    test_tls_discard_cert(&servercertreq);
    test_tls_discard_cert(&servercert1req);
    test_tls_discard_cert(&servercert2req);
    test_tls_discard_cert(&servercert3req);
    test_tls_discard_cert(&servercert4req);
    test_tls_discard_cert(&servercert5req);
    test_tls_discard_cert(&servercert6req);
    test_tls_discard_cert(&servercert7req);
    test_tls_discard_cert(&servercert8req);
    test_tls_discard_cert(&servercert9req);
    test_tls_discard_cert(&servercert10req);
    test_tls_discard_cert(&servercert11req);
    test_tls_discard_cert(&servercert12req);
    test_tls_discard_cert(&servercert13req);
    test_tls_discard_cert(&servercert14req);
    test_tls_discard_cert(&servercert15req);
    test_tls_discard_cert(&servercert16req);

    test_tls_discard_cert(&clientcertreq);
    test_tls_discard_cert(&clientcert1req);
    test_tls_discard_cert(&clientcert2req);
    test_tls_discard_cert(&clientcert3req);
    test_tls_discard_cert(&clientcert4req);
    test_tls_discard_cert(&clientcert5req);
    test_tls_discard_cert(&clientcert6req);
    test_tls_discard_cert(&clientcert7req);
    test_tls_discard_cert(&clientcert8req);
    test_tls_discard_cert(&clientcert9req);
    test_tls_discard_cert(&clientcert10req);

    test_tls_discard_cert(&cacertexpreq);
    test_tls_discard_cert(&servercertexpreq);
    test_tls_discard_cert(&servercertexp1req);
    test_tls_discard_cert(&clientcertexp1req);

    test_tls_discard_cert(&cacertnewreq);
    test_tls_discard_cert(&servercertnewreq);
    test_tls_discard_cert(&servercertnew1req);
    test_tls_discard_cert(&clientcertnew1req);

    test_tls_discard_cert(&cacertrootreq);
    test_tls_discard_cert(&cacertlevel1areq);
    test_tls_discard_cert(&cacertlevel1breq);
    test_tls_discard_cert(&cacertlevel2areq);
    test_tls_discard_cert(&servercertlevel3areq);
    test_tls_discard_cert(&clientcertlevel2breq);
    unlink(WORKDIR "cacertchain-ctx.pem");

    test_tls_cleanup(KEYFILE);
    rmdir(WORKDIR);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#else /* ! QCRYPTO_HAVE_TLS_TEST_SUPPORT */

int
main(void)
{
    return EXIT_SUCCESS;
}

#endif /* ! QCRYPTO_HAVE_TLS_TEST_SUPPORT */
