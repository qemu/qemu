/*
 * QEMU DNS resolver
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
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

#ifndef QIO_DNS_RESOLVER_H
#define QIO_DNS_RESOLVER_H

#include "qapi/qapi-types-sockets.h"
#include "qom/object.h"
#include "io/task.h"

#define TYPE_QIO_DNS_RESOLVER "qio-dns-resolver"
#define QIO_DNS_RESOLVER(obj)                                    \
    OBJECT_CHECK(QIODNSResolver, (obj), TYPE_QIO_DNS_RESOLVER)
#define QIO_DNS_RESOLVER_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(QIODNSResolverClass, klass, TYPE_QIO_DNS_RESOLVER)
#define QIO_DNS_RESOLVER_GET_CLASS(obj)                                  \
    OBJECT_GET_CLASS(QIODNSResolverClass, obj, TYPE_QIO_DNS_RESOLVER)

typedef struct QIODNSResolver QIODNSResolver;
typedef struct QIODNSResolverClass QIODNSResolverClass;

/**
 * QIODNSResolver:
 *
 * The QIODNSResolver class provides a framework for doing
 * DNS resolution on SocketAddress objects, independently
 * of socket creation.
 *
 * <example>
 *   <title>Resolving addresses synchronously</title>
 *   <programlisting>
 *    int mylisten(SocketAddress *addr, Error **errp) {
 *      QIODNSResolver *resolver = qio_dns_resolver_get_instance();
 *      SocketAddress **rawaddrs = NULL;
 *      size_t nrawaddrs = 0;
 *      Error *err = NULL;
 *      QIOChannel **socks = NULL;
 *      size_t nsocks = 0;
 *
 *      if (qio_dns_resolver_lookup_sync(dns, addr, &nrawaddrs,
 *                                       &rawaddrs, errp) < 0) {
 *          return -1;
 *      }
 *
 *      for (i = 0; i < nrawaddrs; i++) {
 *         QIOChannel *sock = qio_channel_new();
 *         Error *local_err = NULL;
 *         qio_channel_listen_sync(sock, rawaddrs[i], &local_err);
 *         if (local_err) {
 *            error_propagate(&err, local_err);
 *         } else {
 *            socks = g_renew(QIOChannelSocket *, socks, nsocks + 1);
 *            socks[nsocks++] = sock;
 *         }
 *         qapi_free_SocketAddress(rawaddrs[i]);
 *      }
 *      g_free(rawaddrs);
 *
 *      if (nsocks == 0) {
 *         error_propagate(errp, err);
 *      } else {
 *         error_free(err);
 *      }
 *   }
 *   </programlisting>
 * </example>
 *
 * <example>
 *   <title>Resolving addresses asynchronously</title>
 *   <programlisting>
 *    typedef struct MyListenData {
 *       Error *err;
 *       QIOChannelSocket **socks;
 *       size_t nsocks;
 *    } MyListenData;
 *
 *    void mylistenresult(QIOTask *task, void *opaque) {
 *      MyListenData *data = opaque;
 *      QIODNSResolver *resolver =
 *         QIO_DNS_RESOLVER(qio_task_get_source(task);
 *      SocketAddress **rawaddrs = NULL;
 *      size_t nrawaddrs = 0;
 *      Error *err = NULL;
 *
 *      if (qio_task_propagate_error(task, &data->err)) {
 *         return;
 *      }
 *
 *      qio_dns_resolver_lookup_result(resolver, task,
 *                                     &nrawaddrs, &rawaddrs);
 *
 *      for (i = 0; i < nrawaddrs; i++) {
 *         QIOChannel *sock = qio_channel_new();
 *         Error *local_err = NULL;
 *         qio_channel_listen_sync(sock, rawaddrs[i], &local_err);
 *         if (local_err) {
 *            error_propagate(&err, local_err);
 *         } else {
 *            socks = g_renew(QIOChannelSocket *, socks, nsocks + 1);
 *            socks[nsocks++] = sock;
 *         }
 *         qapi_free_SocketAddress(rawaddrs[i]);
 *      }
 *      g_free(rawaddrs);
 *
 *      if (nsocks == 0) {
 *         error_propagate(&data->err, err);
 *      } else {
 *         error_free(err);
 *      }
 *    }
 *
 *    void mylisten(SocketAddress *addr, MyListenData *data) {
 *      QIODNSResolver *resolver = qio_dns_resolver_get_instance();
 *      qio_dns_resolver_lookup_async(dns, addr,
 *                                    mylistenresult, data, NULL);
 *    }
 *   </programlisting>
 * </example>
 */
struct QIODNSResolver {
    Object parent;
};

struct QIODNSResolverClass {
    ObjectClass parent;
};


/**
 * qio_dns_resolver_get_instance:
 *
 * Get the singleton dns resolver instance. The caller
 * does not own a reference on the returned object.
 *
 * Returns: the single dns resolver instance
 */
QIODNSResolver *qio_dns_resolver_get_instance(void);

/**
 * qio_dns_resolver_lookup_sync:
 * @resolver: the DNS resolver instance
 * @addr: the address to resolve
 * @naddr: pointer to hold number of resolved addresses
 * @addrs: pointer to hold resolved addresses
 * @errp: pointer to NULL initialized error object
 *
 * This will attempt to resolve the address provided
 * in @addr. If resolution succeeds, @addrs will be filled
 * with all the resolved addresses. @naddrs will specify
 * the number of entries allocated in @addrs. The caller
 * is responsible for freeing each entry in @addrs, as
 * well as @addrs itself. @naddrs is guaranteed to be
 * greater than zero on success.
 *
 * DNS resolution will be done synchronously so execution
 * of the caller may be blocked for an arbitrary length
 * of time.
 *
 * Returns: 0 if resolution was successful, -1 on error
 */
int qio_dns_resolver_lookup_sync(QIODNSResolver *resolver,
                                 SocketAddress *addr,
                                 size_t *naddrs,
                                 SocketAddress ***addrs,
                                 Error **errp);

/**
 * qio_dns_resolver_lookup_async:
 * @resolver: the DNS resolver instance
 * @addr: the address to resolve
 * @func: the callback to invoke on lookup completion
 * @opaque: data blob to pass to @func
 * @notify: the callback to free @opaque, or NULL
 *
 * This will attempt to resolve the address provided
 * in @addr. The callback @func will be invoked when
 * resolution has either completed or failed. On
 * success, the @func should call the method
 * qio_dns_resolver_lookup_result() to obtain the
 * results.
 *
 * DNS resolution will be done asynchronously so execution
 * of the caller will not be blocked.
 */
void qio_dns_resolver_lookup_async(QIODNSResolver *resolver,
                                   SocketAddress *addr,
                                   QIOTaskFunc func,
                                   gpointer opaque,
                                   GDestroyNotify notify);

/**
 * qio_dns_resolver_lookup_result:
 * @resolver: the DNS resolver instance
 * @task: the task object to get results for
 * @naddr: pointer to hold number of resolved addresses
 * @addrs: pointer to hold resolved addresses
 *
 * This method should be called from the callback passed
 * to qio_dns_resolver_lookup_async() in order to obtain
 * results.  @addrs will be filled with all the resolved
 * addresses. @naddrs will specify the number of entries
 * allocated in @addrs. The caller is responsible for
 * freeing each entry in @addrs, as well as @addrs itself.
 */
void qio_dns_resolver_lookup_result(QIODNSResolver *resolver,
                                    QIOTask *task,
                                    size_t *naddrs,
                                    SocketAddress ***addrs);

#endif /* QIO_DNS_RESOLVER_H */
