/*
 * HMP commands related to cryptodev
 *
 * Copyright (c) 2023 Bytedance.Inc
 *
 * Authors:
 *    zhenwei pi<pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-cryptodev.h"
#include "qapi/qmp/qdict.h"


void hmp_info_cryptodev(Monitor *mon, const QDict *qdict)
{
    QCryptodevInfoList *il;
    QCryptodevBackendServiceTypeList *sl;
    QCryptodevBackendClientList *cl;

    for (il = qmp_query_cryptodev(NULL); il; il = il->next) {
        g_autofree char *services = NULL;
        QCryptodevInfo *info = il->value;
        char *tmp_services;

        /* build a string like 'service=[akcipher|mac|hash|cipher]' */
        for (sl = info->service; sl; sl = sl->next) {
            const char *service = QCryptodevBackendServiceType_str(sl->value);

            if (!services) {
                services = g_strdup(service);
            } else {
                tmp_services = g_strjoin("|", services, service, NULL);
                g_free(services);
                services = tmp_services;
            }
        }
        monitor_printf(mon, "%s: service=[%s]\n", info->id, services);

        for (cl = info->client; cl; cl = cl->next) {
            QCryptodevBackendClient *client = cl->value;
            monitor_printf(mon, "    queue %" PRIu32 ": type=%s\n",
                           client->queue,
                           QCryptodevBackendType_str(client->type));
        }
    }

    qapi_free_QCryptodevInfoList(il);
}
