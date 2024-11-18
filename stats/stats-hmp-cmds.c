/*
 * HMP commands related to stats
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-stats.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qemu/cutils.h"
#include "hw/core/cpu.h"
#include "qobject/qdict.h"
#include "qapi/error.h"

static void print_stats_schema_value(Monitor *mon, StatsSchemaValue *value)
{
    const char *unit = NULL;
    monitor_printf(mon, "    %s (%s%s", value->name, StatsType_str(value->type),
                   value->has_unit || value->exponent ? ", " : "");

    if (value->has_unit) {
        if (value->unit == STATS_UNIT_SECONDS) {
            unit = "s";
        } else if (value->unit == STATS_UNIT_BYTES) {
            unit = "B";
        }
    }

    if (unit && value->base == 10 &&
        value->exponent >= -18 && value->exponent <= 18 &&
        value->exponent % 3 == 0) {
        monitor_puts(mon, si_prefix(value->exponent));
    } else if (unit && value->base == 2 &&
               value->exponent >= 0 && value->exponent <= 60 &&
               value->exponent % 10 == 0) {

        monitor_puts(mon, iec_binary_prefix(value->exponent));
    } else if (value->exponent) {
        /* Use exponential notation and write the unit's English name */
        monitor_printf(mon, "* %d^%d%s",
                       value->base, value->exponent,
                       value->has_unit ? " " : "");
        unit = NULL;
    }

    if (value->has_unit) {
        monitor_puts(mon, unit ? unit : StatsUnit_str(value->unit));
    }

    /* Print bucket size for linear histograms */
    if (value->type == STATS_TYPE_LINEAR_HISTOGRAM && value->has_bucket_size) {
        monitor_printf(mon, ", bucket size=%d", value->bucket_size);
    }
    monitor_printf(mon, ")");
}

static StatsSchemaValueList *find_schema_value_list(
    StatsSchemaList *list, StatsProvider provider,
    StatsTarget target)
{
    StatsSchemaList *node;

    for (node = list; node; node = node->next) {
        if (node->value->provider == provider &&
            node->value->target == target) {
            return node->value->stats;
        }
    }
    return NULL;
}

static void print_stats_results(Monitor *mon, StatsTarget target,
                                bool show_provider,
                                StatsResult *result,
                                StatsSchemaList *schema)
{
    /* Find provider schema */
    StatsSchemaValueList *schema_value_list =
        find_schema_value_list(schema, result->provider, target);
    StatsList *stats_list;

    if (!schema_value_list) {
        monitor_printf(mon, "failed to find schema list for %s\n",
                       StatsProvider_str(result->provider));
        return;
    }

    if (show_provider) {
        monitor_printf(mon, "provider: %s\n",
                       StatsProvider_str(result->provider));
    }

    for (stats_list = result->stats; stats_list;
             stats_list = stats_list->next,
             schema_value_list = schema_value_list->next) {

        Stats *stats = stats_list->value;
        StatsValue *stats_value = stats->value;
        StatsSchemaValue *schema_value = schema_value_list->value;

        /* Find schema entry */
        while (!g_str_equal(stats->name, schema_value->name)) {
            if (!schema_value_list->next) {
                monitor_printf(mon, "failed to find schema entry for %s\n",
                               stats->name);
                return;
            }
            schema_value_list = schema_value_list->next;
            schema_value = schema_value_list->value;
        }

        print_stats_schema_value(mon, schema_value);

        if (stats_value->type == QTYPE_QNUM) {
            monitor_printf(mon, ": %" PRId64 "\n", stats_value->u.scalar);
        } else if (stats_value->type == QTYPE_QBOOL) {
            monitor_printf(mon, ": %s\n", stats_value->u.boolean ? "yes" : "no");
        } else if (stats_value->type == QTYPE_QLIST) {
            uint64List *list;
            int i;

            monitor_printf(mon, ": ");
            for (list = stats_value->u.list, i = 1;
                 list;
                 list = list->next, i++) {
                monitor_printf(mon, "[%d]=%" PRId64 " ", i, list->value);
            }
            monitor_printf(mon, "\n");
        }
    }
}

/* Create the StatsFilter that is needed for an "info stats" invocation.  */
static StatsFilter *stats_filter(StatsTarget target, const char *names,
                                 int cpu_index, StatsProvider provider)
{
    StatsFilter *filter = g_malloc0(sizeof(*filter));
    StatsProvider provider_idx;
    StatsRequestList *request_list = NULL;

    filter->target = target;
    switch (target) {
    case STATS_TARGET_VM:
        break;
    case STATS_TARGET_VCPU:
    {
        strList *vcpu_list = NULL;
        CPUState *cpu = qemu_get_cpu(cpu_index);
        char *canonical_path = object_get_canonical_path(OBJECT(cpu));

        QAPI_LIST_PREPEND(vcpu_list, canonical_path);
        filter->u.vcpu.has_vcpus = true;
        filter->u.vcpu.vcpus = vcpu_list;
        break;
    }
    case STATS_TARGET_CRYPTODEV:
        break;
    default:
        break;
    }

    if (!names && provider == STATS_PROVIDER__MAX) {
        return filter;
    }

    /*
     * "info stats" can only query either one or all the providers.  Querying
     * by name, but not by provider, requires the creation of one filter per
     * provider.
     */
    for (provider_idx = 0; provider_idx < STATS_PROVIDER__MAX; provider_idx++) {
        if (provider == STATS_PROVIDER__MAX || provider == provider_idx) {
            StatsRequest *request = g_new0(StatsRequest, 1);
            request->provider = provider_idx;
            if (names && !g_str_equal(names, "*")) {
                request->has_names = true;
                request->names = hmp_split_at_comma(names);
            }
            QAPI_LIST_PREPEND(request_list, request);
        }
    }

    filter->has_providers = true;
    filter->providers = request_list;
    return filter;
}

void hmp_info_stats(Monitor *mon, const QDict *qdict)
{
    const char *target_str = qdict_get_str(qdict, "target");
    const char *provider_str = qdict_get_try_str(qdict, "provider");
    const char *names = qdict_get_try_str(qdict, "names");

    StatsProvider provider = STATS_PROVIDER__MAX;
    StatsTarget target;
    Error *err = NULL;
    g_autoptr(StatsSchemaList) schema = NULL;
    g_autoptr(StatsResultList) stats = NULL;
    g_autoptr(StatsFilter) filter = NULL;
    StatsResultList *entry;

    target = qapi_enum_parse(&StatsTarget_lookup, target_str, -1, &err);
    if (err) {
        monitor_printf(mon, "invalid stats target %s\n", target_str);
        goto exit_no_print;
    }
    if (provider_str) {
        provider = qapi_enum_parse(&StatsProvider_lookup, provider_str, -1, &err);
        if (err) {
            monitor_printf(mon, "invalid stats provider %s\n", provider_str);
            goto exit_no_print;
        }
    }

    schema = qmp_query_stats_schemas(provider_str ? true : false,
                                     provider, &err);
    if (err) {
        goto exit;
    }

    switch (target) {
    case STATS_TARGET_VM:
        filter = stats_filter(target, names, -1, provider);
        break;
    case STATS_TARGET_VCPU: {}
        int cpu_index = monitor_get_cpu_index(mon);
        filter = stats_filter(target, names, cpu_index, provider);
        break;
    case STATS_TARGET_CRYPTODEV:
        filter = stats_filter(target, names, -1, provider);
        break;
    default:
        abort();
    }

    stats = qmp_query_stats(filter, &err);
    if (err) {
        goto exit;
    }
    for (entry = stats; entry; entry = entry->next) {
        print_stats_results(mon, target, provider_str == NULL, entry->value, schema);
    }

exit:
    if (err) {
        monitor_printf(mon, "%s\n", error_get_pretty(err));
    }
exit_no_print:
    error_free(err);
}
