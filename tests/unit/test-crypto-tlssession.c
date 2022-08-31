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
#include "crypto-tls-psk-helpers.h"
#include "crypto/tlscredsx509.h"
#include "crypto/tlscredspsk.h"
#include "crypto/tlssession.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "authz/list.h"

#define WORKDIR "tests/test-crypto-tlssession-work/"
#define PSKFILE WORKDIR "keys.psk"
#define KEYFILE WORKDIR "key-ctx.pem"

static ssize_t testWrite(const char *buf, size_t len, void *opaque)
{
    int *fd = opaque;

    return write(*fd, buf, len);
}

static ssize_t testRead(char *buf, size_t len, void *opaque)
{
    int *fd = opaque;

    return read(*fd, buf, len);
}

static QCryptoTLSCreds *test_tls_creds_psk_create(
    QCryptoTLSCredsEndpoint endpoint,
    const char *dir)
{
    Object *parent = object_get_objects_root();
    Object *creds = object_new_with_props(
        TYPE_QCRYPTO_TLS_CREDS_PSK,
        parent,
        (endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER ?
         "testtlscredsserver" : "testtlscredsclient"),
        &error_abort,
        "endpoint", (endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER ?
                     "server" : "client"),
        "dir", dir,
        "priority", "NORMAL",
        NULL
        );
    return QCRYPTO_TLS_CREDS(creds);
}


static void test_crypto_tls_session_psk(void)
{
    QCryptoTLSCreds *clientCreds;
    QCryptoTLSCreds *serverCreds;
    QCryptoTLSSession *clientSess = NULL;
    QCryptoTLSSession *serverSess = NULL;
    int channel[2];
    bool clientShake = false;
    bool serverShake = false;
    int ret;

    /* We'll use this for our fake client-server connection */
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, channel);
    g_assert(ret == 0);

    /*
     * We have an evil loop to do the handshake in a single
     * thread, so we need these non-blocking to avoid deadlock
     * of ourselves
     */
    qemu_socket_set_nonblock(channel[0]);
    qemu_socket_set_nonblock(channel[1]);

    clientCreds = test_tls_creds_psk_create(
        QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT,
        WORKDIR);
    g_assert(clientCreds != NULL);

    serverCreds = test_tls_creds_psk_create(
        QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
        WORKDIR);
    g_assert(serverCreds != NULL);

    /* Now the real part of the test, setup the sessions */
    clientSess = qcrypto_tls_session_new(
        clientCreds, NULL, NULL,
        QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT, &error_abort);
    g_assert(clientSess != NULL);

    serverSess = qcrypto_tls_session_new(
        serverCreds, NULL, NULL,
        QCRYPTO_TLS_CREDS_ENDPOINT_SERVER, &error_abort);
    g_assert(serverSess != NULL);

    /* For handshake to work, we need to set the I/O callbacks
     * to read/write over the socketpair
     */
    qcrypto_tls_session_set_callbacks(serverSess,
                                      testWrite, testRead,
                                      &channel[0]);
    qcrypto_tls_session_set_callbacks(clientSess,
                                      testWrite, testRead,
                                      &channel[1]);

    /*
     * Finally we loop around & around doing handshake on each
     * session until we get an error, or the handshake completes.
     * This relies on the socketpair being nonblocking to avoid
     * deadlocking ourselves upon handshake
     */
    do {
        int rv;
        if (!serverShake) {
            rv = qcrypto_tls_session_handshake(serverSess,
                                               &error_abort);
            g_assert(rv >= 0);
            if (qcrypto_tls_session_get_handshake_status(serverSess) ==
                QCRYPTO_TLS_HANDSHAKE_COMPLETE) {
                serverShake = true;
            }
        }
        if (!clientShake) {
            rv = qcrypto_tls_session_handshake(clientSess,
                                               &error_abort);
            g_assert(rv >= 0);
            if (qcrypto_tls_session_get_handshake_status(clientSess) ==
                QCRYPTO_TLS_HANDSHAKE_COMPLETE) {
                clientShake = true;
            }
        }
    } while (!clientShake || !serverShake);


    /* Finally make sure the server & client validation is successful. */
    g_assert(qcrypto_tls_session_check_credentials(serverSess,
                                                   &error_abort) == 0);
    g_assert(qcrypto_tls_session_check_credentials(clientSess,
                                                   &error_abort) == 0);

    object_unparent(OBJECT(serverCreds));
    object_unparent(OBJECT(clientCreds));

    qcrypto_tls_session_free(serverSess);
    qcrypto_tls_session_free(clientSess);

    close(channel[0]);
    close(channel[1]);
}


struct QCryptoTLSSessionTestData {
    const char *servercacrt;
    const char *clientcacrt;
    const char *servercrt;
    const char *clientcrt;
    bool expectServerFail;
    bool expectClientFail;
    const char *hostname;
    const char *const *wildcards;
};

static QCryptoTLSCreds *test_tls_creds_x509_create(
    QCryptoTLSCredsEndpoint endpoint,
    const char *certdir)
{
    Object *parent = object_get_objects_root();
    Object *creds = object_new_with_props(
        TYPE_QCRYPTO_TLS_CREDS_X509,
        parent,
        (endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER ?
         "testtlscredsserver" : "testtlscredsclient"),
        &error_abort,
        "endpoint", (endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER ?
                     "server" : "client"),
        "dir", certdir,
        "verify-peer", "yes",
        "priority", "NORMAL",
        /* We skip initial sanity checks here because we
         * want to make sure that problems are being
         * detected at the TLS session validation stage,
         * and the test-crypto-tlscreds test already
         * validate the sanity check code.
         */
        "sanity-check", "no",
        NULL
        );
    return QCRYPTO_TLS_CREDS(creds);
}


/*
 * This tests validation checking of peer certificates
 *
 * This is replicating the checks that are done for an
 * active TLS session after handshake completes. To
 * simulate that we create our TLS contexts, skipping
 * sanity checks. We then get a socketpair, and
 * initiate a TLS session across them. Finally do
 * do actual cert validation tests
 */
static void test_crypto_tls_session_x509(const void *opaque)
{
    struct QCryptoTLSSessionTestData *data =
        (struct QCryptoTLSSessionTestData *)opaque;
    QCryptoTLSCreds *clientCreds;
    QCryptoTLSCreds *serverCreds;
    QCryptoTLSSession *clientSess = NULL;
    QCryptoTLSSession *serverSess = NULL;
    QAuthZList *auth;
    const char * const *wildcards;
    int channel[2];
    bool clientShake = false;
    bool serverShake = false;
    int ret;

    /* We'll use this for our fake client-server connection */
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, channel);
    g_assert(ret == 0);

    /*
     * We have an evil loop to do the handshake in a single
     * thread, so we need these non-blocking to avoid deadlock
     * of ourselves
     */
    qemu_socket_set_nonblock(channel[0]);
    qemu_socket_set_nonblock(channel[1]);

#define CLIENT_CERT_DIR "tests/test-crypto-tlssession-client/"
#define SERVER_CERT_DIR "tests/test-crypto-tlssession-server/"
    g_mkdir_with_parents(CLIENT_CERT_DIR, 0700);
    g_mkdir_with_parents(SERVER_CERT_DIR, 0700);

    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT);
    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY);

    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT);
    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY);

    g_assert(link(data->servercacrt,
                  SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT) == 0);
    g_assert(link(data->servercrt,
                  SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT) == 0);
    g_assert(link(KEYFILE,
                  SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY) == 0);

    g_assert(link(data->clientcacrt,
                  CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT) == 0);
    g_assert(link(data->clientcrt,
                  CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT) == 0);
    g_assert(link(KEYFILE,
                  CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY) == 0);

    clientCreds = test_tls_creds_x509_create(
        QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT,
        CLIENT_CERT_DIR);
    g_assert(clientCreds != NULL);

    serverCreds = test_tls_creds_x509_create(
        QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
        SERVER_CERT_DIR);
    g_assert(serverCreds != NULL);

    auth = qauthz_list_new("tlssessionacl",
                           QAUTHZ_LIST_POLICY_DENY,
                           &error_abort);
    wildcards = data->wildcards;
    while (wildcards && *wildcards) {
        qauthz_list_append_rule(auth, *wildcards,
                                QAUTHZ_LIST_POLICY_ALLOW,
                                QAUTHZ_LIST_FORMAT_GLOB,
                                &error_abort);
        wildcards++;
    }

    /* Now the real part of the test, setup the sessions */
    clientSess = qcrypto_tls_session_new(
        clientCreds, data->hostname, NULL,
        QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT, &error_abort);
    g_assert(clientSess != NULL);

    serverSess = qcrypto_tls_session_new(
        serverCreds, NULL,
        data->wildcards ? "tlssessionacl" : NULL,
        QCRYPTO_TLS_CREDS_ENDPOINT_SERVER, &error_abort);
    g_assert(serverSess != NULL);

    /* For handshake to work, we need to set the I/O callbacks
     * to read/write over the socketpair
     */
    qcrypto_tls_session_set_callbacks(serverSess,
                                      testWrite, testRead,
                                      &channel[0]);
    qcrypto_tls_session_set_callbacks(clientSess,
                                      testWrite, testRead,
                                      &channel[1]);

    /*
     * Finally we loop around & around doing handshake on each
     * session until we get an error, or the handshake completes.
     * This relies on the socketpair being nonblocking to avoid
     * deadlocking ourselves upon handshake
     */
    do {
        int rv;
        if (!serverShake) {
            rv = qcrypto_tls_session_handshake(serverSess,
                                               &error_abort);
            g_assert(rv >= 0);
            if (qcrypto_tls_session_get_handshake_status(serverSess) ==
                QCRYPTO_TLS_HANDSHAKE_COMPLETE) {
                serverShake = true;
            }
        }
        if (!clientShake) {
            rv = qcrypto_tls_session_handshake(clientSess,
                                               &error_abort);
            g_assert(rv >= 0);
            if (qcrypto_tls_session_get_handshake_status(clientSess) ==
                QCRYPTO_TLS_HANDSHAKE_COMPLETE) {
                clientShake = true;
            }
        }
    } while (!clientShake || !serverShake);


    /* Finally make sure the server validation does what
     * we were expecting
     */
    if (qcrypto_tls_session_check_credentials(
            serverSess, data->expectServerFail ? NULL : &error_abort) < 0) {
        g_assert(data->expectServerFail);
    } else {
        g_assert(!data->expectServerFail);
    }

    /*
     * And the same for the client validation check
     */
    if (qcrypto_tls_session_check_credentials(
            clientSess, data->expectClientFail ? NULL : &error_abort) < 0) {
        g_assert(data->expectClientFail);
    } else {
        g_assert(!data->expectClientFail);
    }

    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_CERT);
    unlink(SERVER_CERT_DIR QCRYPTO_TLS_CREDS_X509_SERVER_KEY);

    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CA_CERT);
    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_CERT);
    unlink(CLIENT_CERT_DIR QCRYPTO_TLS_CREDS_X509_CLIENT_KEY);

    rmdir(CLIENT_CERT_DIR);
    rmdir(SERVER_CERT_DIR);

    object_unparent(OBJECT(serverCreds));
    object_unparent(OBJECT(clientCreds));
    object_unparent(OBJECT(auth));

    qcrypto_tls_session_free(serverSess);
    qcrypto_tls_session_free(clientSess);

    close(channel[0]);
    close(channel[1]);
}


int main(int argc, char **argv)
{
    int ret;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);
    g_setenv("GNUTLS_FORCE_FIPS_MODE", "2", 1);

    g_mkdir_with_parents(WORKDIR, 0700);

    test_tls_init(KEYFILE);
    test_tls_psk_init(PSKFILE);

    /* Simple initial test using Pre-Shared Keys. */
    g_test_add_func("/qcrypto/tlssession/psk",
                    test_crypto_tls_session_psk);

    /* More complex tests using X.509 certificates. */
# define TEST_SESS_REG(name, caCrt,                                     \
                       serverCrt, clientCrt,                            \
                       expectServerFail, expectClientFail,              \
                       hostname, wildcards)                             \
    struct QCryptoTLSSessionTestData name = {                           \
        caCrt, caCrt, serverCrt, clientCrt,                             \
        expectServerFail, expectClientFail,                             \
        hostname, wildcards                                             \
    };                                                                  \
    g_test_add_data_func("/qcrypto/tlssession/" # name,                 \
                         &name, test_crypto_tls_session_x509);          \


# define TEST_SESS_REG_EXT(name, serverCaCrt, clientCaCrt,              \
                           serverCrt, clientCrt,                        \
                           expectServerFail, expectClientFail,          \
                           hostname, wildcards)                         \
    struct QCryptoTLSSessionTestData name = {                           \
        serverCaCrt, clientCaCrt, serverCrt, clientCrt,                 \
        expectServerFail, expectClientFail,                             \
        hostname, wildcards                                             \
    };                                                                  \
    g_test_add_data_func("/qcrypto/tlssession/" # name,                 \
                         &name, test_crypto_tls_session_x509);          \

    /* A perfect CA, perfect client & perfect server */

    /* Basic:CA:critical */
    TLS_ROOT_REQ(cacertreq,
                 "UK", "qemu CA", NULL, NULL, NULL, NULL,
                 true, true, true,
                 true, true, GNUTLS_KEY_KEY_CERT_SIGN,
                 false, false, NULL, NULL,
                 0, 0);

    TLS_ROOT_REQ(altcacertreq,
                 "UK", "qemu CA 1", NULL, NULL, NULL, NULL,
                 true, true, true,
                 false, false, 0,
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

    TLS_CERT_REQ(clientcertaltreq, altcacertreq,
                 "UK", "qemu", NULL, NULL, NULL, NULL,
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_CLIENT, NULL,
                 0, 0);

    TEST_SESS_REG(basicca, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  false, false, "qemu.org", NULL);
    TEST_SESS_REG_EXT(differentca, cacertreq.filename,
                      altcacertreq.filename, servercertreq.filename,
                      clientcertaltreq.filename, true, true, "qemu.org", NULL);


    /* When an altname is set, the CN is ignored, so it must be duplicated
     * as an altname for it to match */
    TLS_CERT_REQ(servercertalt1req, cacertreq,
                 "UK", "qemu.org", "www.qemu.org", "qemu.org",
                 "192.168.122.1", "fec0::dead:beaf",
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);
    /* This intentionally doesn't replicate */
    TLS_CERT_REQ(servercertalt2req, cacertreq,
                 "UK", "qemu.org", "www.qemu.org", "wiki.qemu.org",
                 "192.168.122.1", "fec0::dead:beaf",
                 true, true, false,
                 true, true,
                 GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,
                 true, true, GNUTLS_KP_TLS_WWW_SERVER, NULL,
                 0, 0);

    TEST_SESS_REG(altname1, cacertreq.filename,
                  servercertalt1req.filename, clientcertreq.filename,
                  false, false, "qemu.org", NULL);
    TEST_SESS_REG(altname2, cacertreq.filename,
                  servercertalt1req.filename, clientcertreq.filename,
                  false, false, "www.qemu.org", NULL);
    TEST_SESS_REG(altname3, cacertreq.filename,
                  servercertalt1req.filename, clientcertreq.filename,
                  false, true, "wiki.qemu.org", NULL);

    TEST_SESS_REG(altname4, cacertreq.filename,
                  servercertalt1req.filename, clientcertreq.filename,
                  false, false, "192.168.122.1", NULL);
    TEST_SESS_REG(altname5, cacertreq.filename,
                  servercertalt1req.filename, clientcertreq.filename,
                  false, false, "fec0::dead:beaf", NULL);

    TEST_SESS_REG(altname6, cacertreq.filename,
                  servercertalt2req.filename, clientcertreq.filename,
                  false, true, "qemu.org", NULL);
    TEST_SESS_REG(altname7, cacertreq.filename,
                  servercertalt2req.filename, clientcertreq.filename,
                  false, false, "www.qemu.org", NULL);
    TEST_SESS_REG(altname8, cacertreq.filename,
                  servercertalt2req.filename, clientcertreq.filename,
                  false, false, "wiki.qemu.org", NULL);

    const char *const wildcards1[] = {
        "C=UK,CN=dogfood",
        NULL,
    };
    const char *const wildcards2[] = {
        "C=UK,CN=qemu",
        NULL,
    };
    const char *const wildcards3[] = {
        "C=UK,CN=dogfood",
        "C=UK,CN=qemu",
        NULL,
    };
    const char *const wildcards4[] = {
        "C=UK,CN=qemustuff",
        NULL,
    };
    const char *const wildcards5[] = {
        "C=UK,CN=qemu*",
        NULL,
    };
    const char *const wildcards6[] = {
        "C=UK,CN=*emu*",
        NULL,
    };

    TEST_SESS_REG(wildcard1, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  true, false, "qemu.org", wildcards1);
    TEST_SESS_REG(wildcard2, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  false, false, "qemu.org", wildcards2);
    TEST_SESS_REG(wildcard3, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  false, false, "qemu.org", wildcards3);
    TEST_SESS_REG(wildcard4, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  true, false, "qemu.org", wildcards4);
    TEST_SESS_REG(wildcard5, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  false, false, "qemu.org", wildcards5);
    TEST_SESS_REG(wildcard6, cacertreq.filename,
                  servercertreq.filename, clientcertreq.filename,
                  false, false, "qemu.org", wildcards6);

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

    test_tls_write_cert_chain(WORKDIR "cacertchain-sess.pem",
                              certchain,
                              G_N_ELEMENTS(certchain));

    TEST_SESS_REG(cachain, WORKDIR "cacertchain-sess.pem",
                  servercertlevel3areq.filename, clientcertlevel2breq.filename,
                  false, false, "qemu.org", NULL);

    ret = g_test_run();

    test_tls_discard_cert(&clientcertreq);
    test_tls_discard_cert(&clientcertaltreq);

    test_tls_discard_cert(&servercertreq);
    test_tls_discard_cert(&servercertalt1req);
    test_tls_discard_cert(&servercertalt2req);

    test_tls_discard_cert(&cacertreq);
    test_tls_discard_cert(&altcacertreq);

    test_tls_discard_cert(&cacertrootreq);
    test_tls_discard_cert(&cacertlevel1areq);
    test_tls_discard_cert(&cacertlevel1breq);
    test_tls_discard_cert(&cacertlevel2areq);
    test_tls_discard_cert(&servercertlevel3areq);
    test_tls_discard_cert(&clientcertlevel2breq);
    unlink(WORKDIR "cacertchain-sess.pem");

    test_tls_psk_cleanup(PSKFILE);
    test_tls_cleanup(KEYFILE);
    rmdir(WORKDIR);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
