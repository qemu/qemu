/*
 * QEMU migration TLS support
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
#include "channel.h"
#include "migration.h"
#include "tls.h"
#include "options.h"
#include "crypto/tlscreds.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

static QCryptoTLSCreds *
migration_tls_get_creds(QCryptoTLSCredsEndpoint endpoint, Error **errp)
{
    Object *creds;
    const char *tls_creds = migrate_tls_creds();
    QCryptoTLSCreds *ret;

    creds = object_resolve_path_component(object_get_objects_root(), tls_creds);
    if (!creds) {
        error_setg(errp, "No TLS credentials with id '%s'", tls_creds);
        return NULL;
    }
    ret = (QCryptoTLSCreds *)object_dynamic_cast(
        creds, TYPE_QCRYPTO_TLS_CREDS);
    if (!ret) {
        error_setg(errp, "Object with id '%s' is not TLS credentials",
                   tls_creds);
        return NULL;
    }
    if (!qcrypto_tls_creds_check_endpoint(ret, endpoint, errp)) {
        return NULL;
    }

    return ret;
}


static void migration_tls_incoming_handshake(QIOTask *task,
                                             gpointer opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        trace_migration_tls_incoming_handshake_error(error_get_pretty(err));
        error_report_err(err);
    } else {
        trace_migration_tls_incoming_handshake_complete();
        migration_channel_process_incoming(ioc);
    }
    object_unref(OBJECT(ioc));
}

void migration_tls_channel_process_incoming(MigrationState *s,
                                            QIOChannel *ioc,
                                            Error **errp)
{
    QCryptoTLSCreds *creds;
    QIOChannelTLS *tioc;

    creds = migration_tls_get_creds(QCRYPTO_TLS_CREDS_ENDPOINT_SERVER, errp);
    if (!creds) {
        return;
    }

    tioc = qio_channel_tls_new_server(ioc, creds, migrate_tls_authz(), errp);
    if (!tioc) {
        return;
    }

    trace_migration_tls_incoming_handshake_start();
    qio_channel_set_name(QIO_CHANNEL(tioc), "migration-tls-incoming");
    qio_channel_tls_handshake(tioc,
                              migration_tls_incoming_handshake,
                              NULL,
                              NULL,
                              NULL);
}


static void migration_tls_outgoing_handshake(QIOTask *task,
                                             gpointer opaque)
{
    MigrationState *s = opaque;
    QIOChannel *ioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        trace_migration_tls_outgoing_handshake_error(error_get_pretty(err));
    } else {
        trace_migration_tls_outgoing_handshake_complete();
    }
    migration_channel_connect(s, ioc, NULL, err);
    object_unref(OBJECT(ioc));
}

QIOChannelTLS *migration_tls_client_create(QIOChannel *ioc,
                                           const char *hostname,
                                           Error **errp)
{
    QCryptoTLSCreds *creds;

    creds = migration_tls_get_creds(QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT, errp);
    if (!creds) {
        return NULL;
    }

    const char *tls_hostname = migrate_tls_hostname();
    if (tls_hostname && *tls_hostname) {
        hostname = tls_hostname;
    }

    return qio_channel_tls_new_client(ioc, creds, hostname, errp);
}

void migration_tls_channel_connect(MigrationState *s,
                                   QIOChannel *ioc,
                                   const char *hostname,
                                   Error **errp)
{
    QIOChannelTLS *tioc;

    tioc = migration_tls_client_create(ioc, hostname, errp);
    if (!tioc) {
        return;
    }

    /* Save hostname into MigrationState for handshake */
    s->hostname = g_strdup(hostname);
    trace_migration_tls_outgoing_handshake_start(hostname);
    qio_channel_set_name(QIO_CHANNEL(tioc), "migration-tls-outgoing");
    qio_channel_tls_handshake(tioc,
                              migration_tls_outgoing_handshake,
                              s,
                              NULL,
                              NULL);
}

bool migrate_channel_requires_tls_upgrade(QIOChannel *ioc)
{
    if (!migrate_tls()) {
        return false;
    }

    return !object_dynamic_cast(OBJECT(ioc), TYPE_QIO_CHANNEL_TLS);
}
