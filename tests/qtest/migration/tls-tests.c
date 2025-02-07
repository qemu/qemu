/*
 * QTest testcases for TLS migration
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "crypto/tlscredspsk.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

#include "tests/unit/crypto-tls-psk-helpers.h"
#ifdef CONFIG_TASN1
# include "tests/unit/crypto-tls-x509-helpers.h"
#endif /* CONFIG_TASN1 */


struct TestMigrateTLSPSKData {
    char *workdir;
    char *workdiralt;
    char *pskfile;
    char *pskfilealt;
};

static char *tmpfs;

static void *
migrate_hook_start_tls_psk_common(QTestState *from,
                                  QTestState *to,
                                  bool mismatch)
{
    struct TestMigrateTLSPSKData *data =
        g_new0(struct TestMigrateTLSPSKData, 1);

    data->workdir = g_strdup_printf("%s/tlscredspsk0", tmpfs);
    data->pskfile = g_strdup_printf("%s/%s", data->workdir,
                                    QCRYPTO_TLS_CREDS_PSKFILE);
    g_mkdir_with_parents(data->workdir, 0700);
    test_tls_psk_init(data->pskfile);

    if (mismatch) {
        data->workdiralt = g_strdup_printf("%s/tlscredspskalt0", tmpfs);
        data->pskfilealt = g_strdup_printf("%s/%s", data->workdiralt,
                                           QCRYPTO_TLS_CREDS_PSKFILE);
        g_mkdir_with_parents(data->workdiralt, 0700);
        test_tls_psk_init_alt(data->pskfilealt);
    }

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'username': 'qemu'} }",
                             data->workdir);

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s } }",
                             mismatch ? data->workdiralt : data->workdir);

    migrate_set_parameter_str(from, "tls-creds", "tlscredspsk0");
    migrate_set_parameter_str(to, "tls-creds", "tlscredspsk0");

    return data;
}

static void *
migrate_hook_start_tls_psk_match(QTestState *from,
                                 QTestState *to)
{
    return migrate_hook_start_tls_psk_common(from, to, false);
}

static void *
migrate_hook_start_tls_psk_mismatch(QTestState *from,
                                    QTestState *to)
{
    return migrate_hook_start_tls_psk_common(from, to, true);
}

static void
migrate_hook_end_tls_psk(QTestState *from,
                         QTestState *to,
                         void *opaque)
{
    struct TestMigrateTLSPSKData *data = opaque;

    test_tls_psk_cleanup(data->pskfile);
    if (data->pskfilealt) {
        test_tls_psk_cleanup(data->pskfilealt);
    }
    rmdir(data->workdir);
    if (data->workdiralt) {
        rmdir(data->workdiralt);
    }

    g_free(data->workdiralt);
    g_free(data->pskfilealt);
    g_free(data->workdir);
    g_free(data->pskfile);
    g_free(data);
}

#ifdef CONFIG_TASN1
typedef struct {
    char *workdir;
    char *keyfile;
    char *cacert;
    char *servercert;
    char *serverkey;
    char *clientcert;
    char *clientkey;
} TestMigrateTLSX509Data;

typedef struct {
    bool verifyclient;
    bool clientcert;
    bool hostileclient;
    bool authzclient;
    const char *certhostname;
    const char *certipaddr;
} TestMigrateTLSX509;

static void *
migrate_hook_start_tls_x509_common(QTestState *from,
                                   QTestState *to,
                                   TestMigrateTLSX509 *args)
{
    TestMigrateTLSX509Data *data = g_new0(TestMigrateTLSX509Data, 1);

    data->workdir = g_strdup_printf("%s/tlscredsx5090", tmpfs);
    data->keyfile = g_strdup_printf("%s/key.pem", data->workdir);

    data->cacert = g_strdup_printf("%s/ca-cert.pem", data->workdir);
    data->serverkey = g_strdup_printf("%s/server-key.pem", data->workdir);
    data->servercert = g_strdup_printf("%s/server-cert.pem", data->workdir);
    if (args->clientcert) {
        data->clientkey = g_strdup_printf("%s/client-key.pem", data->workdir);
        data->clientcert = g_strdup_printf("%s/client-cert.pem", data->workdir);
    }

    g_mkdir_with_parents(data->workdir, 0700);

    test_tls_init(data->keyfile);
#ifndef _WIN32
    g_assert(link(data->keyfile, data->serverkey) == 0);
#else
    g_assert(CreateHardLink(data->serverkey, data->keyfile, NULL) != 0);
#endif
    if (args->clientcert) {
#ifndef _WIN32
        g_assert(link(data->keyfile, data->clientkey) == 0);
#else
        g_assert(CreateHardLink(data->clientkey, data->keyfile, NULL) != 0);
#endif
    }

    TLS_ROOT_REQ_SIMPLE(cacertreq, data->cacert);
    if (args->clientcert) {
        TLS_CERT_REQ_SIMPLE_CLIENT(servercertreq, cacertreq,
                                   args->hostileclient ?
                                   QCRYPTO_TLS_TEST_CLIENT_HOSTILE_NAME :
                                   QCRYPTO_TLS_TEST_CLIENT_NAME,
                                   data->clientcert);
        test_tls_deinit_cert(&servercertreq);
    }

    TLS_CERT_REQ_SIMPLE_SERVER(clientcertreq, cacertreq,
                               data->servercert,
                               args->certhostname,
                               args->certipaddr);
    test_tls_deinit_cert(&clientcertreq);
    test_tls_deinit_cert(&cacertreq);

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509client0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': true} }",
                             data->workdir);
    migrate_set_parameter_str(from, "tls-creds", "tlscredsx509client0");
    if (args->certhostname) {
        migrate_set_parameter_str(from, "tls-hostname", args->certhostname);
    }

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509server0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': %i} }",
                             data->workdir, args->verifyclient);
    migrate_set_parameter_str(to, "tls-creds", "tlscredsx509server0");

    if (args->authzclient) {
        qtest_qmp_assert_success(to,
                                 "{ 'execute': 'object-add',"
                                 "  'arguments': { 'qom-type': 'authz-simple',"
                                 "                 'id': 'tlsauthz0',"
                                 "                 'identity': %s} }",
                                 "CN=" QCRYPTO_TLS_TEST_CLIENT_NAME);
        migrate_set_parameter_str(to, "tls-authz", "tlsauthz0");
    }

    return data;
}

/*
 * The normal case: match server's cert hostname against
 * whatever host we were telling QEMU to connect to (if any)
 */
static void *
migrate_hook_start_tls_x509_default_host(QTestState *from,
                                         QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "127.0.0.1"
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to (if any),
 * so we must give QEMU an explicit hostname to validate
 */
static void *
migrate_hook_start_tls_x509_override_host(QTestState *from,
                                          QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certhostname = "qemu.org",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to, and so we
 * expect the client to reject the server
 */
static void *
migrate_hook_start_tls_x509_mismatch_host(QTestState *from,
                                          QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "10.0.0.1",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

static void *
migrate_hook_start_tls_x509_friendly_client(QTestState *from,
                                            QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

static void *
migrate_hook_start_tls_x509_hostile_client(QTestState *from,
                                           QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .hostileclient = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and no server verification
 */
static void *
migrate_hook_start_tls_x509_allow_anon_client(QTestState *from,
                                              QTestState *to)
{
    TestMigrateTLSX509 args = {
        .certipaddr = "127.0.0.1",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and server verification rejecting
 */
static void *
migrate_hook_start_tls_x509_reject_anon_client(QTestState *from,
                                               QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .certipaddr = "127.0.0.1",
    };
    return migrate_hook_start_tls_x509_common(from, to, &args);
}

static void
migrate_hook_end_tls_x509(QTestState *from,
                          QTestState *to,
                          void *opaque)
{
    TestMigrateTLSX509Data *data = opaque;

    test_tls_cleanup(data->keyfile);
    g_free(data->keyfile);

    unlink(data->cacert);
    g_free(data->cacert);
    unlink(data->servercert);
    g_free(data->servercert);
    unlink(data->serverkey);
    g_free(data->serverkey);

    if (data->clientcert) {
        unlink(data->clientcert);
        g_free(data->clientcert);
    }
    if (data->clientkey) {
        unlink(data->clientkey);
        g_free(data->clientkey);
    }

    rmdir(data->workdir);
    g_free(data->workdir);

    g_free(data);
}
#endif /* CONFIG_TASN1 */

static void test_postcopy_tls_psk(void)
{
    MigrateCommon args = {
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_postcopy_common(&args);
}

static void test_postcopy_preempt_tls_psk(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_postcopy_common(&args);
}

static void test_postcopy_recovery_tls_psk(void)
{
    MigrateCommon args = {
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_postcopy_recovery_common(&args);
}

/* This contains preempt+recovery+tls test altogether */
static void test_postcopy_preempt_all(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_postcopy_recovery_common(&args);
}

static void test_precopy_unix_tls_psk(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_precopy_unix_tls_x509_default_host(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = migrate_hook_start_tls_x509_default_host,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_tls_x509_override_host(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = migrate_hook_start_tls_x509_override_host,
        .end_hook = migrate_hook_end_tls_x509,
    };

    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */

static void test_precopy_tcp_tls_psk_match(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_psk_mismatch(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_psk_mismatch,
        .end_hook = migrate_hook_end_tls_psk,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_precopy_tcp_tls_x509_default_host(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_default_host,
        .end_hook = migrate_hook_end_tls_x509,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_override_host(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_override_host,
        .end_hook = migrate_hook_end_tls_x509,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_mismatch_host(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_mismatch_host,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_friendly_client(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_friendly_client,
        .end_hook = migrate_hook_end_tls_x509,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_hostile_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_hostile_client,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_allow_anon_client(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_allow_anon_client,
        .end_hook = migrate_hook_end_tls_x509,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_reject_anon_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = migrate_hook_start_tls_x509_reject_anon_client,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */

static void *
migrate_hook_start_multifd_tcp_tls_psk_match(QTestState *from,
                                             QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_psk_match(from, to);
}

static void *
migrate_hook_start_multifd_tcp_tls_psk_mismatch(QTestState *from,
                                                QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_psk_mismatch(from, to);
}

#ifdef CONFIG_TASN1
static void *
migrate_hook_start_multifd_tls_x509_default_host(QTestState *from,
                                                 QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_x509_default_host(from, to);
}

static void *
migrate_hook_start_multifd_tls_x509_override_host(QTestState *from,
                                                  QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_x509_override_host(from, to);
}

static void *
migrate_hook_start_multifd_tls_x509_mismatch_host(QTestState *from,
                                                  QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_x509_mismatch_host(from, to);
}

static void *
migrate_hook_start_multifd_tls_x509_allow_anon_client(QTestState *from,
                                                      QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_x509_allow_anon_client(from, to);
}

static void *
migrate_hook_start_multifd_tls_x509_reject_anon_client(QTestState *from,
                                                       QTestState *to)
{
    migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
    return migrate_hook_start_tls_x509_reject_anon_client(from, to);
}
#endif /* CONFIG_TASN1 */

static void test_multifd_tcp_tls_psk_match(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tcp_tls_psk_match,
        .end_hook = migrate_hook_end_tls_psk,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_psk_mismatch(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tcp_tls_psk_mismatch,
        .end_hook = migrate_hook_end_tls_psk,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_multifd_tcp_tls_x509_default_host(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tls_x509_default_host,
        .end_hook = migrate_hook_end_tls_x509,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_override_host(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tls_x509_override_host,
        .end_hook = migrate_hook_end_tls_x509,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_mismatch_host(void)
{
    /*
     * This has different behaviour to the non-multifd case.
     *
     * In non-multifd case when client aborts due to mismatched
     * cert host, the server has already started trying to load
     * migration state, and so it exits with I/O failure.
     *
     * In multifd case when client aborts due to mismatched
     * cert host, the server is still waiting for the other
     * multifd connections to arrive so hasn't started trying
     * to load migration state, and thus just aborts the migration
     * without exiting.
     */
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tls_x509_mismatch_host,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_allow_anon_client(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tls_x509_allow_anon_client,
        .end_hook = migrate_hook_end_tls_x509,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_reject_anon_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_tls_x509_reject_anon_client,
        .end_hook = migrate_hook_end_tls_x509,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */

static void migration_test_add_tls_smoke(MigrationTestEnv *env)
{
    migration_test_add("/migration/precopy/tcp/tls/psk/match",
                       test_precopy_tcp_tls_psk_match);
}

void migration_test_add_tls(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_tls_smoke(env);

    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/precopy/unix/tls/psk",
                       test_precopy_unix_tls_psk);

    if (env->has_uffd) {
        /*
         * NOTE: psk test is enough for postcopy, as other types of TLS
         * channels are tested under precopy.  Here what we want to test is the
         * general postcopy path that has TLS channel enabled.
         */
        migration_test_add("/migration/postcopy/tls/psk",
                           test_postcopy_tls_psk);
        migration_test_add("/migration/postcopy/recovery/tls/psk",
                           test_postcopy_recovery_tls_psk);
        migration_test_add("/migration/postcopy/preempt/tls/psk",
                           test_postcopy_preempt_tls_psk);
        migration_test_add("/migration/postcopy/preempt/recovery/tls/psk",
                           test_postcopy_preempt_all);
    }
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/unix/tls/x509/default-host",
                       test_precopy_unix_tls_x509_default_host);
    migration_test_add("/migration/precopy/unix/tls/x509/override-host",
                       test_precopy_unix_tls_x509_override_host);
#endif /* CONFIG_TASN1 */

    migration_test_add("/migration/precopy/tcp/tls/psk/mismatch",
                       test_precopy_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/tcp/tls/x509/default-host",
                       test_precopy_tcp_tls_x509_default_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/override-host",
                       test_precopy_tcp_tls_x509_override_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/mismatch-host",
                       test_precopy_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/friendly-client",
                       test_precopy_tcp_tls_x509_friendly_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/hostile-client",
                       test_precopy_tcp_tls_x509_hostile_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/allow-anon-client",
                       test_precopy_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/reject-anon-client",
                       test_precopy_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */

    migration_test_add("/migration/multifd/tcp/tls/psk/match",
                       test_multifd_tcp_tls_psk_match);
    migration_test_add("/migration/multifd/tcp/tls/psk/mismatch",
                       test_multifd_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    migration_test_add("/migration/multifd/tcp/tls/x509/default-host",
                       test_multifd_tcp_tls_x509_default_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/override-host",
                       test_multifd_tcp_tls_x509_override_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/mismatch-host",
                       test_multifd_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/allow-anon-client",
                       test_multifd_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/multifd/tcp/tls/x509/reject-anon-client",
                       test_multifd_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
}
