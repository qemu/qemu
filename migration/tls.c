/*
 * QEMU migration TLS support
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
#include "migration/migration.h"
#include "io/channel-tls.h"
#include "crypto/tlscreds.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

static QCryptoTLSCreds *
migration_tls_get_creds(MigrationState *s,
                        QCryptoTLSCredsEndpoint endpoint,
                        Error **errp)
{
    Object *creds;
    QCryptoTLSCreds *ret;

    creds = object_resolve_path_component(
        object_get_objects_root(), s->parameters.tls_creds);
    if (!creds) {
        error_setg(errp, "No TLS credentials with id '%s'",
                   s->parameters.tls_creds);
        return NULL;
    }
    ret = (QCryptoTLSCreds *)object_dynamic_cast(
        creds, TYPE_QCRYPTO_TLS_CREDS);
    if (!ret) {
        error_setg(errp, "Object with id '%s' is not TLS credentials",
                   s->parameters.tls_creds);
        return NULL;
    }
    if (ret->endpoint != endpoint) {
        error_setg(errp,
                   "Expected TLS credentials for a %s endpoint",
                   endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT ?
                   "client" : "server");
        return NULL;
    }

    object_ref(OBJECT(ret));
    return ret;
}


static void migration_tls_incoming_handshake(Object *src,
                                             Error *err,
                                             gpointer opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(src);

    if (err) {
        trace_migration_tls_incoming_handshake_error(error_get_pretty(err));
        error_report("%s", error_get_pretty(err));
    } else {
        trace_migration_tls_incoming_handshake_complete();
        migration_channel_process_incoming(migrate_get_current(), ioc);
    }
    object_unref(OBJECT(ioc));
}

void migration_tls_channel_process_incoming(MigrationState *s,
                                            QIOChannel *ioc,
                                            Error **errp)
{
    QCryptoTLSCreds *creds;
    QIOChannelTLS *tioc;

    creds = migration_tls_get_creds(
        s, QCRYPTO_TLS_CREDS_ENDPOINT_SERVER, errp);
    if (!creds) {
        return;
    }

    tioc = qio_channel_tls_new_server(
        ioc, creds,
        NULL, /* XXX pass ACL name */
        errp);
    if (!tioc) {
        return;
    }

    trace_migration_tls_incoming_handshake_start();
    qio_channel_tls_handshake(tioc,
                              migration_tls_incoming_handshake,
                              NULL,
                              NULL);
}


static void migration_tls_outgoing_handshake(Object *src,
                                             Error *err,
                                             gpointer opaque)
{
    MigrationState *s = opaque;
    QIOChannel *ioc = QIO_CHANNEL(src);

    if (err) {
        trace_migration_tls_outgoing_handshake_error(error_get_pretty(err));
        s->to_dst_file = NULL;
        migrate_fd_error(s, err);
    } else {
        trace_migration_tls_outgoing_handshake_complete();
        migration_channel_connect(s, ioc, NULL);
    }
    object_unref(OBJECT(ioc));
}


void migration_tls_channel_connect(MigrationState *s,
                                   QIOChannel *ioc,
                                   const char *hostname,
                                   Error **errp)
{
    QCryptoTLSCreds *creds;
    QIOChannelTLS *tioc;

    creds = migration_tls_get_creds(
        s, QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT, errp);
    if (!creds) {
        return;
    }

    if (s->parameters.tls_hostname) {
        hostname = s->parameters.tls_hostname;
    }
    if (!hostname) {
        error_setg(errp, "No hostname available for TLS");
        return;
    }

    tioc = qio_channel_tls_new_client(
        ioc, creds, hostname, errp);
    if (!tioc) {
        return;
    }

    trace_migration_tls_outgoing_handshake_start(hostname);
    qio_channel_tls_handshake(tioc,
                              migration_tls_outgoing_handshake,
                              s,
                              NULL);
}
