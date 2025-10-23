/*
 * HMP commands related to migration
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "block/qapi.h"
#include "migration/snapshot.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qobject/qdict.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "system/runstate.h"
#include "ui/qemu-spice.h"
#include "system/system.h"
#include "options.h"
#include "migration.h"

static void migration_global_dump(Monitor *mon)
{
    MigrationState *ms = migrate_get_current();

    monitor_printf(mon, "Globals:\n");
    monitor_printf(mon, "  store-global-state: %s\n",
                   ms->store_global_state ? "on" : "off");
    monitor_printf(mon, "  only-migratable: %s\n",
                   only_migratable ? "on" : "off");
    monitor_printf(mon, "  send-configuration: %s\n",
                   ms->send_configuration ? "on" : "off");
    monitor_printf(mon, "  send-section-footer: %s\n",
                   ms->send_section_footer ? "on" : "off");
    monitor_printf(mon, "  send-switchover-start: %s\n",
                   ms->send_switchover_start ? "on" : "off");
    monitor_printf(mon, "  clear-bitmap-shift: %u\n",
                   ms->clear_bitmap_shift);
}

static const gchar *format_time_str(uint64_t us)
{
    const char *units[] = {"us", "ms", "sec"};
    int index = 0;

    while (us >= 1000 && index + 1 < ARRAY_SIZE(units)) {
        us /= 1000;
        index++;
    }

    return g_strdup_printf("%"PRIu64" %s", us, units[index]);
}

static void migration_dump_blocktime(Monitor *mon, MigrationInfo *info)
{
    if (info->has_postcopy_blocktime) {
        monitor_printf(mon, "Postcopy Blocktime (ms): %" PRIu32 "\n",
                       info->postcopy_blocktime);
    }

    if (info->has_postcopy_vcpu_blocktime) {
        uint32List *item = info->postcopy_vcpu_blocktime;
        const char *sep = "";
        int count = 0;

        monitor_printf(mon, "Postcopy vCPU Blocktime (ms):\n [");

        while (item) {
            monitor_printf(mon, "%s%"PRIu32, sep, item->value);
            item = item->next;
            /* Each line 10 vcpu results, newline if there's more */
            sep = ((++count % 10 == 0) && item) ? ",\n  " : ", ";
        }
        monitor_printf(mon, "]\n");
    }

    if (info->has_postcopy_latency) {
        monitor_printf(mon, "Postcopy Latency (ns): %" PRIu64 "\n",
                       info->postcopy_latency);
    }

    if (info->has_postcopy_non_vcpu_latency) {
        monitor_printf(mon, "Postcopy non-vCPU Latencies (ns): %" PRIu64 "\n",
                       info->postcopy_non_vcpu_latency);
    }

    if (info->has_postcopy_vcpu_latency) {
        uint64List *item = info->postcopy_vcpu_latency;
        const char *sep = "";
        int count = 0;

        monitor_printf(mon, "Postcopy vCPU Latencies (ns):\n [");

        while (item) {
            monitor_printf(mon, "%s%"PRIu64, sep, item->value);
            item = item->next;
            /* Each line 10 vcpu results, newline if there's more */
            sep = ((++count % 10 == 0) && item) ? ",\n  " : ", ";
        }
        monitor_printf(mon, "]\n");
    }

    if (info->has_postcopy_latency_dist) {
        uint64List *item = info->postcopy_latency_dist;
        int count = 0;

        monitor_printf(mon, "Postcopy Latency Distribution:\n");

        while (item) {
            g_autofree const gchar *from = format_time_str(1UL << count);
            g_autofree const gchar *to = format_time_str(1UL << (count + 1));

            monitor_printf(mon, "  [ %8s - %8s ]: %10"PRIu64"\n",
                           from, to, item->value);
            item = item->next;
            count++;
        }
    }
}

void hmp_info_migrate(Monitor *mon, const QDict *qdict)
{
    bool show_all = qdict_get_try_bool(qdict, "all", false);
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);

    if (info->blocked_reasons) {
        strList *reasons = info->blocked_reasons;
        monitor_printf(mon, "Outgoing migration blocked:\n");
        while (reasons) {
            monitor_printf(mon, "  %s\n", reasons->value);
            reasons = reasons->next;
        }
    }

    if (info->has_status) {
        monitor_printf(mon, "Status: \t\t%s",
                       MigrationStatus_str(info->status));
        if ((info->status == MIGRATION_STATUS_FAILED ||
             info->status == MIGRATION_STATUS_POSTCOPY_PAUSED) &&
            info->error_desc) {
            monitor_printf(mon, " (%s)\n", info->error_desc);
        } else {
            monitor_printf(mon, "\n");
        }

        if (info->total_time) {
            monitor_printf(mon, "Time (ms): \t\ttotal=%" PRIu64,
                           info->total_time);
            if (info->has_setup_time) {
                monitor_printf(mon, ", setup=%" PRIu64,
                               info->setup_time);
            }
            if (info->has_expected_downtime) {
                monitor_printf(mon, ", exp_down=%" PRIu64,
                               info->expected_downtime);
            }
            if (info->has_downtime) {
                monitor_printf(mon, ", down=%" PRIu64,
                               info->downtime);
            }
            monitor_printf(mon, "\n");
        }
    }

    if (info->has_socket_address) {
        SocketAddressList *addr;

        monitor_printf(mon, "Sockets: [\n");

        for (addr = info->socket_address; addr; addr = addr->next) {
            char *s = socket_uri(addr->value);
            monitor_printf(mon, "\t%s\n", s);
            g_free(s);
        }
        monitor_printf(mon, "]\n");
    }

    if (info->ram) {
        g_autofree char *str_psize = size_to_str(info->ram->page_size);
        g_autofree char *str_total = size_to_str(info->ram->total);
        g_autofree char *str_transferred = size_to_str(info->ram->transferred);
        g_autofree char *str_remaining = size_to_str(info->ram->remaining);
        g_autofree char *str_precopy = size_to_str(info->ram->precopy_bytes);
        g_autofree char *str_multifd = size_to_str(info->ram->multifd_bytes);
        g_autofree char *str_postcopy = size_to_str(info->ram->postcopy_bytes);

        monitor_printf(mon, "RAM info:\n");
        monitor_printf(mon, "  Throughput (Mbps): \t%0.2f\n",
                       info->ram->mbps);
        monitor_printf(mon, "  Sizes: \t\tpagesize=%s, total=%s\n",
                       str_psize, str_total);
        monitor_printf(mon, "  Transfers: \t\ttransferred=%s, remain=%s\n",
                       str_transferred, str_remaining);
        monitor_printf(mon, "    Channels: \t\tprecopy=%s, "
                       "multifd=%s, postcopy=%s",
                       str_precopy, str_multifd, str_postcopy);

        if (info->vfio) {
            g_autofree char *str_vfio = size_to_str(info->vfio->transferred);

            monitor_printf(mon, ", vfio=%s", str_vfio);
        }
        monitor_printf(mon, "\n");

        monitor_printf(mon, "    Page Types: \tnormal=%" PRIu64
                       ", zero=%" PRIu64 "\n",
                       info->ram->normal, info->ram->duplicate);
        monitor_printf(mon, "  Page Rates (pps): \ttransfer=%" PRIu64,
                       info->ram->pages_per_second);
        if (info->ram->dirty_pages_rate) {
            monitor_printf(mon, ", dirty=%" PRIu64,
                           info->ram->dirty_pages_rate);
        }
        monitor_printf(mon, "\n");

        monitor_printf(mon, "  Others: \t\tdirty_syncs=%" PRIu64,
                       info->ram->dirty_sync_count);
        if (info->ram->postcopy_requests) {
            monitor_printf(mon, ", postcopy_req=%" PRIu64,
                           info->ram->postcopy_requests);
        }
        if (info->ram->downtime_bytes) {
            monitor_printf(mon, ", downtime_bytes=%" PRIu64,
                           info->ram->downtime_bytes);
        }
        if (info->ram->dirty_sync_missed_zero_copy) {
            monitor_printf(mon, ", zerocopy_fallbacks=%" PRIu64,
                           info->ram->dirty_sync_missed_zero_copy);
        }
        monitor_printf(mon, "\n");
    }

    if (!show_all) {
        goto out;
    }

    migration_global_dump(mon);

    if (info->xbzrle_cache) {
        monitor_printf(mon, "XBZRLE: size=%" PRIu64
                       ", transferred=%" PRIu64
                       ", pages=%" PRIu64
                       ", miss=%" PRIu64 "\n"
                       "  miss_rate=%0.2f"
                       ", encode_rate=%0.2f"
                       ", overflow=%" PRIu64 "\n",
                       info->xbzrle_cache->cache_size,
                       info->xbzrle_cache->bytes,
                       info->xbzrle_cache->pages,
                       info->xbzrle_cache->cache_miss,
                       info->xbzrle_cache->cache_miss_rate,
                       info->xbzrle_cache->encoding_rate,
                       info->xbzrle_cache->overflow);
    }

    if (info->has_cpu_throttle_percentage) {
        monitor_printf(mon, "CPU Throttle (%%): %" PRIu64 "\n",
                       info->cpu_throttle_percentage);
    }

    if (info->has_dirty_limit_throttle_time_per_round) {
        monitor_printf(mon, "Dirty-limit Throttle (us): %" PRIu64 "\n",
                       info->dirty_limit_throttle_time_per_round);
    }

    if (info->has_dirty_limit_ring_full_time) {
        monitor_printf(mon, "Dirty-limit Ring Full (us): %" PRIu64 "\n",
                       info->dirty_limit_ring_full_time);
    }

    migration_dump_blocktime(mon, info);
out:
    qapi_free_MigrationInfo(info);
}

void hmp_info_migrate_capabilities(Monitor *mon, const QDict *qdict)
{
    MigrationCapabilityStatusList *caps, *cap;

    caps = qmp_query_migrate_capabilities(NULL);

    if (caps) {
        for (cap = caps; cap; cap = cap->next) {
            monitor_printf(mon, "%s: %s\n",
                           MigrationCapability_str(cap->value->capability),
                           cap->value->state ? "on" : "off");
        }
    }

    qapi_free_MigrationCapabilityStatusList(caps);
}

static void monitor_print_cpr_exec_command(Monitor *mon, strList *args)
{
    monitor_printf(mon, "%s:",
        MigrationParameter_str(MIGRATION_PARAMETER_CPR_EXEC_COMMAND));

    while (args) {
        monitor_printf(mon, " %s", args->value);
        args = args->next;
    }
    monitor_printf(mon, "\n");
}

void hmp_info_migrate_parameters(Monitor *mon, const QDict *qdict)
{
    MigrationParameters *params;

    params = qmp_query_migrate_parameters(NULL);

    if (params) {
        monitor_printf(mon, "%s: %" PRIu64 " ms\n",
            MigrationParameter_str(MIGRATION_PARAMETER_ANNOUNCE_INITIAL),
            params->announce_initial);
        monitor_printf(mon, "%s: %" PRIu64 " ms\n",
            MigrationParameter_str(MIGRATION_PARAMETER_ANNOUNCE_MAX),
            params->announce_max);
        monitor_printf(mon, "%s: %" PRIu64 "\n",
            MigrationParameter_str(MIGRATION_PARAMETER_ANNOUNCE_ROUNDS),
            params->announce_rounds);
        monitor_printf(mon, "%s: %" PRIu64 " ms\n",
            MigrationParameter_str(MIGRATION_PARAMETER_ANNOUNCE_STEP),
            params->announce_step);
        assert(params->has_throttle_trigger_threshold);
        monitor_printf(mon, "%s: %u\n",
            MigrationParameter_str(MIGRATION_PARAMETER_THROTTLE_TRIGGER_THRESHOLD),
            params->throttle_trigger_threshold);
        assert(params->has_cpu_throttle_initial);
        monitor_printf(mon, "%s: %u\n",
            MigrationParameter_str(MIGRATION_PARAMETER_CPU_THROTTLE_INITIAL),
            params->cpu_throttle_initial);
        assert(params->has_cpu_throttle_increment);
        monitor_printf(mon, "%s: %u\n",
            MigrationParameter_str(MIGRATION_PARAMETER_CPU_THROTTLE_INCREMENT),
            params->cpu_throttle_increment);
        assert(params->has_cpu_throttle_tailslow);
        monitor_printf(mon, "%s: %s\n",
            MigrationParameter_str(MIGRATION_PARAMETER_CPU_THROTTLE_TAILSLOW),
            params->cpu_throttle_tailslow ? "on" : "off");
        assert(params->has_max_cpu_throttle);
        monitor_printf(mon, "%s: %u\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MAX_CPU_THROTTLE),
            params->max_cpu_throttle);
        assert(params->tls_creds);
        monitor_printf(mon, "%s: '%s'\n",
            MigrationParameter_str(MIGRATION_PARAMETER_TLS_CREDS),
            params->tls_creds);
        assert(params->tls_hostname);
        monitor_printf(mon, "%s: '%s'\n",
            MigrationParameter_str(MIGRATION_PARAMETER_TLS_HOSTNAME),
            params->tls_hostname);
        assert(params->tls_authz);
        monitor_printf(mon, "%s: '%s'\n",
            MigrationParameter_str(MIGRATION_PARAMETER_TLS_AUTHZ),
            params->tls_authz);
        assert(params->has_max_bandwidth);
        monitor_printf(mon, "%s: %" PRIu64 " bytes/second\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MAX_BANDWIDTH),
            params->max_bandwidth);
        assert(params->has_avail_switchover_bandwidth);
        monitor_printf(mon, "%s: %" PRIu64 " bytes/second\n",
            MigrationParameter_str(MIGRATION_PARAMETER_AVAIL_SWITCHOVER_BANDWIDTH),
            params->avail_switchover_bandwidth);
        assert(params->has_max_postcopy_bandwidth);
        monitor_printf(mon, "%s: %" PRIu64 " bytes/second\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MAX_POSTCOPY_BANDWIDTH),
            params->max_postcopy_bandwidth);
        assert(params->has_downtime_limit);
        monitor_printf(mon, "%s: %" PRIu64 " ms\n",
            MigrationParameter_str(MIGRATION_PARAMETER_DOWNTIME_LIMIT),
            params->downtime_limit);
        assert(params->has_x_checkpoint_delay);
        monitor_printf(mon, "%s: %u ms\n",
            MigrationParameter_str(MIGRATION_PARAMETER_X_CHECKPOINT_DELAY),
            params->x_checkpoint_delay);
        monitor_printf(mon, "%s: %u\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MULTIFD_CHANNELS),
            params->multifd_channels);
        monitor_printf(mon, "%s: %s\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MULTIFD_COMPRESSION),
            MultiFDCompression_str(params->multifd_compression));
        assert(params->has_zero_page_detection);
        monitor_printf(mon, "%s: %s\n",
            MigrationParameter_str(MIGRATION_PARAMETER_ZERO_PAGE_DETECTION),
            qapi_enum_lookup(&ZeroPageDetection_lookup,
                params->zero_page_detection));
        monitor_printf(mon, "%s: %" PRIu64 " bytes\n",
            MigrationParameter_str(MIGRATION_PARAMETER_XBZRLE_CACHE_SIZE),
            params->xbzrle_cache_size);

        if (params->has_block_bitmap_mapping) {
            const BitmapMigrationNodeAliasList *bmnal;

            monitor_printf(mon, "%s:\n",
                           MigrationParameter_str(
                               MIGRATION_PARAMETER_BLOCK_BITMAP_MAPPING));

            for (bmnal = params->block_bitmap_mapping;
                 bmnal;
                 bmnal = bmnal->next)
            {
                const BitmapMigrationNodeAlias *bmna = bmnal->value;
                const BitmapMigrationBitmapAliasList *bmbal;

                monitor_printf(mon, "  '%s' -> '%s'\n",
                               bmna->node_name, bmna->alias);

                for (bmbal = bmna->bitmaps; bmbal; bmbal = bmbal->next) {
                    const BitmapMigrationBitmapAlias *bmba = bmbal->value;

                    monitor_printf(mon, "    '%s' -> '%s'\n",
                                   bmba->name, bmba->alias);
                }
            }
        }

        monitor_printf(mon, "%s: %" PRIu64 " ms\n",
        MigrationParameter_str(MIGRATION_PARAMETER_X_VCPU_DIRTY_LIMIT_PERIOD),
        params->x_vcpu_dirty_limit_period);

        monitor_printf(mon, "%s: %" PRIu64 " MB/s\n",
            MigrationParameter_str(MIGRATION_PARAMETER_VCPU_DIRTY_LIMIT),
            params->vcpu_dirty_limit);

        assert(params->has_mode);
        monitor_printf(mon, "%s: %s\n",
            MigrationParameter_str(MIGRATION_PARAMETER_MODE),
            qapi_enum_lookup(&MigMode_lookup, params->mode));

        if (params->has_direct_io) {
            monitor_printf(mon, "%s: %s\n",
                           MigrationParameter_str(
                               MIGRATION_PARAMETER_DIRECT_IO),
                           params->direct_io ? "on" : "off");
        }

        assert(params->has_cpr_exec_command);
        monitor_print_cpr_exec_command(mon, params->cpr_exec_command);
    }

    qapi_free_MigrationParameters(params);
}

void hmp_loadvm(Monitor *mon, const QDict *qdict)
{
    RunState saved_state = runstate_get();

    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    vm_stop(RUN_STATE_RESTORE_VM);

    if (load_snapshot(name, NULL, false, NULL, &err)) {
        load_snapshot_resume(saved_state);
    }

    hmp_handle_error(mon, err);
}

void hmp_savevm(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    save_snapshot(qdict_get_try_str(qdict, "name"),
                  true, NULL, false, NULL, &err);
    hmp_handle_error(mon, err);
}

void hmp_delvm(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *name = qdict_get_str(qdict, "name");

    delete_snapshot(name, false, NULL, &err);
    hmp_handle_error(mon, err);
}

void hmp_migrate_cancel(Monitor *mon, const QDict *qdict)
{
    qmp_migrate_cancel(NULL);
}

void hmp_migrate_continue(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *state = qdict_get_str(qdict, "state");
    int val = qapi_enum_parse(&MigrationStatus_lookup, state, -1, &err);

    if (val >= 0) {
        qmp_migrate_continue(val, &err);
    }

    hmp_handle_error(mon, err);
}

void hmp_migrate_incoming(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *uri = qdict_get_str(qdict, "uri");
    MigrationChannelList *caps = NULL;
    g_autoptr(MigrationChannel) channel = NULL;

    if (!migrate_uri_parse(uri, &channel, &err)) {
        goto end;
    }
    QAPI_LIST_PREPEND(caps, g_steal_pointer(&channel));

    qmp_migrate_incoming(NULL, true, caps, true, false, &err);
    qapi_free_MigrationChannelList(caps);

end:
    hmp_handle_error(mon, err);
}

void hmp_migrate_recover(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *uri = qdict_get_str(qdict, "uri");

    qmp_migrate_recover(uri, &err);

    hmp_handle_error(mon, err);
}

void hmp_migrate_pause(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_migrate_pause(&err);

    hmp_handle_error(mon, err);
}


void hmp_migrate_set_capability(Monitor *mon, const QDict *qdict)
{
    const char *cap = qdict_get_str(qdict, "capability");
    bool state = qdict_get_bool(qdict, "state");
    Error *err = NULL;
    MigrationCapabilityStatusList *caps = NULL;
    MigrationCapabilityStatus *value;
    int val;

    val = qapi_enum_parse(&MigrationCapability_lookup, cap, -1, &err);
    if (val < 0) {
        goto end;
    }

    value = g_malloc0(sizeof(*value));
    value->capability = val;
    value->state = state;
    QAPI_LIST_PREPEND(caps, value);
    qmp_migrate_set_capabilities(caps, &err);
    qapi_free_MigrationCapabilityStatusList(caps);

end:
    hmp_handle_error(mon, err);
}

void hmp_migrate_set_parameter(Monitor *mon, const QDict *qdict)
{
    const char *param = qdict_get_str(qdict, "parameter");
    const char *valuestr = qdict_get_str(qdict, "value");
    Visitor *v = string_input_visitor_new(valuestr);
    MigrateSetParameters *p = g_new0(MigrateSetParameters, 1);
    uint64_t valuebw = 0;
    uint64_t cache_size;
    Error *err = NULL;
    int val, ret;

    val = qapi_enum_parse(&MigrationParameter_lookup, param, -1, &err);
    if (val < 0) {
        goto cleanup;
    }

    switch (val) {
    case MIGRATION_PARAMETER_THROTTLE_TRIGGER_THRESHOLD:
        p->has_throttle_trigger_threshold = true;
        visit_type_uint8(v, param, &p->throttle_trigger_threshold, &err);
        break;
    case MIGRATION_PARAMETER_CPU_THROTTLE_INITIAL:
        p->has_cpu_throttle_initial = true;
        visit_type_uint8(v, param, &p->cpu_throttle_initial, &err);
        break;
    case MIGRATION_PARAMETER_CPU_THROTTLE_INCREMENT:
        p->has_cpu_throttle_increment = true;
        visit_type_uint8(v, param, &p->cpu_throttle_increment, &err);
        break;
    case MIGRATION_PARAMETER_CPU_THROTTLE_TAILSLOW:
        p->has_cpu_throttle_tailslow = true;
        visit_type_bool(v, param, &p->cpu_throttle_tailslow, &err);
        break;
    case MIGRATION_PARAMETER_MAX_CPU_THROTTLE:
        p->has_max_cpu_throttle = true;
        visit_type_uint8(v, param, &p->max_cpu_throttle, &err);
        break;
    case MIGRATION_PARAMETER_TLS_CREDS:
        p->tls_creds = g_new0(StrOrNull, 1);
        p->tls_creds->type = QTYPE_QSTRING;
        visit_type_str(v, param, &p->tls_creds->u.s, &err);
        break;
    case MIGRATION_PARAMETER_TLS_HOSTNAME:
        p->tls_hostname = g_new0(StrOrNull, 1);
        p->tls_hostname->type = QTYPE_QSTRING;
        visit_type_str(v, param, &p->tls_hostname->u.s, &err);
        break;
    case MIGRATION_PARAMETER_TLS_AUTHZ:
        p->tls_authz = g_new0(StrOrNull, 1);
        p->tls_authz->type = QTYPE_QSTRING;
        visit_type_str(v, param, &p->tls_authz->u.s, &err);
        break;
    case MIGRATION_PARAMETER_MAX_BANDWIDTH:
        p->has_max_bandwidth = true;
        /*
         * Can't use visit_type_size() here, because it
         * defaults to Bytes rather than Mebibytes.
         */
        ret = qemu_strtosz_MiB(valuestr, NULL, &valuebw);
        if (ret < 0 || valuebw > INT64_MAX
            || (size_t)valuebw != valuebw) {
            error_setg(&err, "Invalid size %s", valuestr);
            break;
        }
        p->max_bandwidth = valuebw;
        break;
    case MIGRATION_PARAMETER_AVAIL_SWITCHOVER_BANDWIDTH:
        p->has_avail_switchover_bandwidth = true;
        ret = qemu_strtosz_MiB(valuestr, NULL, &valuebw);
        if (ret < 0 || valuebw > INT64_MAX
            || (size_t)valuebw != valuebw) {
            error_setg(&err, "Invalid size %s", valuestr);
            break;
        }
        p->avail_switchover_bandwidth = valuebw;
        break;
    case MIGRATION_PARAMETER_DOWNTIME_LIMIT:
        p->has_downtime_limit = true;
        visit_type_size(v, param, &p->downtime_limit, &err);
        break;
    case MIGRATION_PARAMETER_X_CHECKPOINT_DELAY:
        p->has_x_checkpoint_delay = true;
        visit_type_uint32(v, param, &p->x_checkpoint_delay, &err);
        break;
    case MIGRATION_PARAMETER_MULTIFD_CHANNELS:
        p->has_multifd_channels = true;
        visit_type_uint8(v, param, &p->multifd_channels, &err);
        break;
    case MIGRATION_PARAMETER_MULTIFD_COMPRESSION:
        p->has_multifd_compression = true;
        visit_type_MultiFDCompression(v, param, &p->multifd_compression,
                                      &err);
        break;
    case MIGRATION_PARAMETER_MULTIFD_ZLIB_LEVEL:
        p->has_multifd_zlib_level = true;
        visit_type_uint8(v, param, &p->multifd_zlib_level, &err);
        break;
    case MIGRATION_PARAMETER_MULTIFD_QATZIP_LEVEL:
        p->has_multifd_qatzip_level = true;
        visit_type_uint8(v, param, &p->multifd_qatzip_level, &err);
        break;
    case MIGRATION_PARAMETER_MULTIFD_ZSTD_LEVEL:
        p->has_multifd_zstd_level = true;
        visit_type_uint8(v, param, &p->multifd_zstd_level, &err);
        break;
    case MIGRATION_PARAMETER_ZERO_PAGE_DETECTION:
        p->has_zero_page_detection = true;
        visit_type_ZeroPageDetection(v, param, &p->zero_page_detection, &err);
        break;
    case MIGRATION_PARAMETER_XBZRLE_CACHE_SIZE:
        p->has_xbzrle_cache_size = true;
        if (!visit_type_size(v, param, &cache_size, &err)) {
            break;
        }
        if (cache_size > INT64_MAX || (size_t)cache_size != cache_size) {
            error_setg(&err, "Invalid size %s", valuestr);
            break;
        }
        p->xbzrle_cache_size = cache_size;
        break;
    case MIGRATION_PARAMETER_MAX_POSTCOPY_BANDWIDTH:
        p->has_max_postcopy_bandwidth = true;
        visit_type_size(v, param, &p->max_postcopy_bandwidth, &err);
        break;
    case MIGRATION_PARAMETER_ANNOUNCE_INITIAL:
        p->has_announce_initial = true;
        visit_type_size(v, param, &p->announce_initial, &err);
        break;
    case MIGRATION_PARAMETER_ANNOUNCE_MAX:
        p->has_announce_max = true;
        visit_type_size(v, param, &p->announce_max, &err);
        break;
    case MIGRATION_PARAMETER_ANNOUNCE_ROUNDS:
        p->has_announce_rounds = true;
        visit_type_size(v, param, &p->announce_rounds, &err);
        break;
    case MIGRATION_PARAMETER_ANNOUNCE_STEP:
        p->has_announce_step = true;
        visit_type_size(v, param, &p->announce_step, &err);
        break;
    case MIGRATION_PARAMETER_BLOCK_BITMAP_MAPPING:
        error_setg(&err, "The block-bitmap-mapping parameter can only be set "
                   "through QMP");
        break;
    case MIGRATION_PARAMETER_X_VCPU_DIRTY_LIMIT_PERIOD:
        p->has_x_vcpu_dirty_limit_period = true;
        visit_type_size(v, param, &p->x_vcpu_dirty_limit_period, &err);
        break;
    case MIGRATION_PARAMETER_VCPU_DIRTY_LIMIT:
        p->has_vcpu_dirty_limit = true;
        visit_type_size(v, param, &p->vcpu_dirty_limit, &err);
        break;
    case MIGRATION_PARAMETER_MODE:
        p->has_mode = true;
        visit_type_MigMode(v, param, &p->mode, &err);
        break;
    case MIGRATION_PARAMETER_DIRECT_IO:
        p->has_direct_io = true;
        visit_type_bool(v, param, &p->direct_io, &err);
        break;
    case MIGRATION_PARAMETER_CPR_EXEC_COMMAND: {
        /*
         * NOTE: g_autofree will only auto g_free() the strv array when
         * needed, it will not free the strings within the array. It's
         * intentional: when strv is set, the ownership of the strings will
         * always be passed to p->cpr_exec_command via QAPI_LIST_APPEND().
         */
        g_autofree char **strv = NULL;
        g_autoptr(GError) gerr = NULL;
        strList **tail = &p->cpr_exec_command;

        if (!g_shell_parse_argv(valuestr, NULL, &strv, &gerr)) {
            error_setg(&err, "%s", gerr->message);
            break;
        }
        for (int i = 0; strv[i]; i++) {
            QAPI_LIST_APPEND(tail, strv[i]);
        }
        p->has_cpr_exec_command = true;
        break;
    }
    default:
        g_assert_not_reached();
    }

    if (err) {
        goto cleanup;
    }

    qmp_migrate_set_parameters(p, &err);

 cleanup:
    qapi_free_MigrateSetParameters(p);
    visit_free(v);
    hmp_handle_error(mon, err);
}

void hmp_migrate_start_postcopy(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    qmp_migrate_start_postcopy(&err);
    hmp_handle_error(mon, err);
}

#ifdef CONFIG_REPLICATION
void hmp_x_colo_lost_heartbeat(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_x_colo_lost_heartbeat(&err);
    hmp_handle_error(mon, err);
}
#endif

typedef struct HMPMigrationStatus {
    QEMUTimer *timer;
    Monitor *mon;
} HMPMigrationStatus;

static void hmp_migrate_status_cb(void *opaque)
{
    HMPMigrationStatus *status = opaque;
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);
    if (!info->has_status || info->status == MIGRATION_STATUS_ACTIVE ||
        info->status == MIGRATION_STATUS_SETUP) {
        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
    } else {
        if (info->error_desc) {
            error_report("%s", info->error_desc);
        }
        monitor_resume(status->mon);
        timer_free(status->timer);
        g_free(status);
    }

    qapi_free_MigrationInfo(info);
}

void hmp_migrate(Monitor *mon, const QDict *qdict)
{
    bool detach = qdict_get_try_bool(qdict, "detach", false);
    bool resume = qdict_get_try_bool(qdict, "resume", false);
    const char *uri = qdict_get_str(qdict, "uri");
    Error *err = NULL;
    g_autoptr(MigrationChannelList) caps = NULL;
    g_autoptr(MigrationChannel) channel = NULL;

    if (!migrate_uri_parse(uri, &channel, &err)) {
        hmp_handle_error(mon, err);
        return;
    }
    QAPI_LIST_PREPEND(caps, g_steal_pointer(&channel));

    qmp_migrate(NULL, true, caps, false, false, true, resume, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    if (!detach) {
        HMPMigrationStatus *status;

        if (monitor_suspend(mon) < 0) {
            monitor_printf(mon, "terminal does not allow synchronous "
                           "migration, continuing detached\n");
            return;
        }

        status = g_malloc0(sizeof(*status));
        status->mon = mon;
        status->timer = timer_new_ms(QEMU_CLOCK_REALTIME, hmp_migrate_status_cb,
                                          status);
        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }
}

void migrate_set_capability_completion(ReadLineState *rs, int nb_args,
                                       const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
            readline_add_completion_of(rs, str, MigrationCapability_str(i));
        }
    } else if (nb_args == 3) {
        readline_add_completion_of(rs, str, "on");
        readline_add_completion_of(rs, str, "off");
    }
}

void migrate_set_parameter_completion(ReadLineState *rs, int nb_args,
                                      const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_PARAMETER__MAX; i++) {
            readline_add_completion_of(rs, str, MigrationParameter_str(i));
        }
    }
}

static void vm_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    BlockDriverState *bs;
    BdrvNextIterator it;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    len = strlen(str);
    readline_set_completion_index(rs, len);

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        SnapshotInfoList *snapshots, *snapshot;
        bool ok = false;

        if (bdrv_can_snapshot(bs)) {
            ok = bdrv_query_snapshot_info_list(bs, &snapshots, NULL) == 0;
        }
        if (!ok) {
            continue;
        }

        snapshot = snapshots;
        while (snapshot) {
            readline_add_completion_of(rs, str, snapshot->value->name);
            readline_add_completion_of(rs, str, snapshot->value->id);
            snapshot = snapshot->next;
        }
        qapi_free_SnapshotInfoList(snapshots);
    }

}

void delvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

void loadvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}
