/*
 * QMP commands related to stats
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "sysemu/stats.h"
#include "qapi/qapi-commands-stats.h"
#include "qemu/queue.h"
#include "qapi/error.h"

typedef struct StatsCallbacks {
    StatsProvider provider;
    StatRetrieveFunc *stats_cb;
    SchemaRetrieveFunc *schemas_cb;
    QTAILQ_ENTRY(StatsCallbacks) next;
} StatsCallbacks;

static QTAILQ_HEAD(, StatsCallbacks) stats_callbacks =
    QTAILQ_HEAD_INITIALIZER(stats_callbacks);

void add_stats_callbacks(StatsProvider provider,
                         StatRetrieveFunc *stats_fn,
                         SchemaRetrieveFunc *schemas_fn)
{
    StatsCallbacks *entry = g_new(StatsCallbacks, 1);
    entry->provider = provider;
    entry->stats_cb = stats_fn;
    entry->schemas_cb = schemas_fn;

    QTAILQ_INSERT_TAIL(&stats_callbacks, entry, next);
}

static bool invoke_stats_cb(StatsCallbacks *entry,
                            StatsResultList **stats_results,
                            StatsFilter *filter, StatsRequest *request,
                            Error **errp)
{
    ERRP_GUARD();
    strList *targets = NULL;
    strList *names = NULL;

    if (request) {
        if (request->provider != entry->provider) {
            return true;
        }
        if (request->has_names && !request->names) {
            return true;
        }
        names = request->has_names ? request->names : NULL;
    }

    switch (filter->target) {
    case STATS_TARGET_VM:
        break;
    case STATS_TARGET_VCPU:
        if (filter->u.vcpu.has_vcpus) {
            if (!filter->u.vcpu.vcpus) {
                /* No targets allowed?  Return no statistics.  */
                return true;
            }
            targets = filter->u.vcpu.vcpus;
        }
        break;
    default:
        abort();
    }

    entry->stats_cb(stats_results, filter->target, names, targets, errp);
    if (*errp) {
        qapi_free_StatsResultList(*stats_results);
        *stats_results = NULL;
        return false;
    }
    return true;
}

StatsResultList *qmp_query_stats(StatsFilter *filter, Error **errp)
{
    StatsResultList *stats_results = NULL;
    StatsCallbacks *entry;
    StatsRequestList *request;

    QTAILQ_FOREACH(entry, &stats_callbacks, next) {
        if (filter->has_providers) {
            for (request = filter->providers; request; request = request->next) {
                if (!invoke_stats_cb(entry, &stats_results, filter,
                                     request->value, errp)) {
                    break;
                }
            }
        } else {
            if (!invoke_stats_cb(entry, &stats_results, filter, NULL, errp)) {
                break;
            }
        }
    }

    return stats_results;
}

StatsSchemaList *qmp_query_stats_schemas(bool has_provider,
                                         StatsProvider provider,
                                         Error **errp)
{
    ERRP_GUARD();
    StatsSchemaList *stats_results = NULL;
    StatsCallbacks *entry;

    QTAILQ_FOREACH(entry, &stats_callbacks, next) {
        if (!has_provider || provider == entry->provider) {
            entry->schemas_cb(&stats_results, errp);
            if (*errp) {
                qapi_free_StatsSchemaList(stats_results);
                return NULL;
            }
        }
    }

    return stats_results;
}

void add_stats_entry(StatsResultList **stats_results, StatsProvider provider,
                     const char *qom_path, StatsList *stats_list)
{
    StatsResult *entry = g_new0(StatsResult, 1);

    entry->provider = provider;
    entry->qom_path = g_strdup(qom_path);
    entry->stats = stats_list;

    QAPI_LIST_PREPEND(*stats_results, entry);
}

void add_stats_schema(StatsSchemaList **schema_results,
                      StatsProvider provider, StatsTarget target,
                      StatsSchemaValueList *stats_list)
{
    StatsSchema *entry = g_new0(StatsSchema, 1);

    entry->provider = provider;
    entry->target = target;
    entry->stats = stats_list;
    QAPI_LIST_PREPEND(*schema_results, entry);
}

bool apply_str_list_filter(const char *string, strList *list)
{
    strList *str_list = NULL;

    if (!list) {
        return true;
    }
    for (str_list = list; str_list; str_list = str_list->next) {
        if (g_str_equal(string, str_list->value)) {
            return true;
        }
    }
    return false;
}
