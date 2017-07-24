/*
 * Human Monitor Interface
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
#include "hmp.h"
#include "net/net.h"
#include "net/eth.h"
#include "chardev/char.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "qmp-commands.h"
#include "qemu/sockets.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "qapi/opts-visitor.h"
#include "qapi/qmp/qerror.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qapi/util.h"
#include "qapi-visit.h"
#include "qom/object_interfaces.h"
#include "ui/console.h"
#include "block/nbd.h"
#include "block/qapi.h"
#include "qemu-io.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "exec/ramlist.h"
#include "hw/intc/intc.h"
#include "migration/snapshot.h"
#include "migration/misc.h"

#ifdef CONFIG_SPICE
#include <spice/enums.h>
#endif

static void hmp_handle_error(Monitor *mon, Error **errp)
{
    assert(errp);
    if (*errp) {
        error_report_err(*errp);
    }
}

void hmp_info_name(Monitor *mon, const QDict *qdict)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->has_name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qapi_free_NameInfo(info);
}

void hmp_info_version(Monitor *mon, const QDict *qdict)
{
    VersionInfo *info;

    info = qmp_query_version(NULL);

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                   info->qemu->major, info->qemu->minor, info->qemu->micro,
                   info->package);

    qapi_free_VersionInfo(info);
}

void hmp_info_kvm(Monitor *mon, const QDict *qdict)
{
    KvmInfo *info;

    info = qmp_query_kvm(NULL);
    monitor_printf(mon, "kvm support: ");
    if (info->present) {
        monitor_printf(mon, "%s\n", info->enabled ? "enabled" : "disabled");
    } else {
        monitor_printf(mon, "not compiled\n");
    }

    qapi_free_KvmInfo(info);
}

void hmp_info_status(Monitor *mon, const QDict *qdict)
{
    StatusInfo *info;

    info = qmp_query_status(NULL);

    monitor_printf(mon, "VM status: %s%s",
                   info->running ? "running" : "paused",
                   info->singlestep ? " (single step mode)" : "");

    if (!info->running && info->status != RUN_STATE_PAUSED) {
        monitor_printf(mon, " (%s)", RunState_lookup[info->status]);
    }

    monitor_printf(mon, "\n");

    qapi_free_StatusInfo(info);
}

void hmp_info_uuid(Monitor *mon, const QDict *qdict)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qapi_free_UuidInfo(info);
}

void hmp_info_chardev(Monitor *mon, const QDict *qdict)
{
    ChardevInfoList *char_info, *info;

    char_info = qmp_query_chardev(NULL);
    for (info = char_info; info; info = info->next) {
        monitor_printf(mon, "%s: filename=%s\n", info->value->label,
                                                 info->value->filename);
    }

    qapi_free_ChardevInfoList(char_info);
}

void hmp_info_mice(Monitor *mon, const QDict *qdict)
{
    MouseInfoList *mice_list, *mouse;

    mice_list = qmp_query_mice(NULL);
    if (!mice_list) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    for (mouse = mice_list; mouse; mouse = mouse->next) {
        monitor_printf(mon, "%c Mouse #%" PRId64 ": %s%s\n",
                       mouse->value->current ? '*' : ' ',
                       mouse->value->index, mouse->value->name,
                       mouse->value->absolute ? " (absolute)" : "");
    }

    qapi_free_MouseInfoList(mice_list);
}

void hmp_info_migrate(Monitor *mon, const QDict *qdict)
{
    MigrationInfo *info;
    MigrationCapabilityStatusList *caps, *cap;

    info = qmp_query_migrate(NULL);
    caps = qmp_query_migrate_capabilities(NULL);

    migration_global_dump(mon);

    /* do not display parameters during setup */
    if (info->has_status && caps) {
        monitor_printf(mon, "capabilities: ");
        for (cap = caps; cap; cap = cap->next) {
            monitor_printf(mon, "%s: %s ",
                           MigrationCapability_lookup[cap->value->capability],
                           cap->value->state ? "on" : "off");
        }
        monitor_printf(mon, "\n");
    }

    if (info->has_status) {
        monitor_printf(mon, "Migration status: %s",
                       MigrationStatus_lookup[info->status]);
        if (info->status == MIGRATION_STATUS_FAILED &&
            info->has_error_desc) {
            monitor_printf(mon, " (%s)\n", info->error_desc);
        } else {
            monitor_printf(mon, "\n");
        }

        monitor_printf(mon, "total time: %" PRIu64 " milliseconds\n",
                       info->total_time);
        if (info->has_expected_downtime) {
            monitor_printf(mon, "expected downtime: %" PRIu64 " milliseconds\n",
                           info->expected_downtime);
        }
        if (info->has_downtime) {
            monitor_printf(mon, "downtime: %" PRIu64 " milliseconds\n",
                           info->downtime);
        }
        if (info->has_setup_time) {
            monitor_printf(mon, "setup: %" PRIu64 " milliseconds\n",
                           info->setup_time);
        }
    }

    if (info->has_ram) {
        monitor_printf(mon, "transferred ram: %" PRIu64 " kbytes\n",
                       info->ram->transferred >> 10);
        monitor_printf(mon, "throughput: %0.2f mbps\n",
                       info->ram->mbps);
        monitor_printf(mon, "remaining ram: %" PRIu64 " kbytes\n",
                       info->ram->remaining >> 10);
        monitor_printf(mon, "total ram: %" PRIu64 " kbytes\n",
                       info->ram->total >> 10);
        monitor_printf(mon, "duplicate: %" PRIu64 " pages\n",
                       info->ram->duplicate);
        monitor_printf(mon, "skipped: %" PRIu64 " pages\n",
                       info->ram->skipped);
        monitor_printf(mon, "normal: %" PRIu64 " pages\n",
                       info->ram->normal);
        monitor_printf(mon, "normal bytes: %" PRIu64 " kbytes\n",
                       info->ram->normal_bytes >> 10);
        monitor_printf(mon, "dirty sync count: %" PRIu64 "\n",
                       info->ram->dirty_sync_count);
        monitor_printf(mon, "page size: %" PRIu64 " kbytes\n",
                       info->ram->page_size >> 10);

        if (info->ram->dirty_pages_rate) {
            monitor_printf(mon, "dirty pages rate: %" PRIu64 " pages\n",
                           info->ram->dirty_pages_rate);
        }
        if (info->ram->postcopy_requests) {
            monitor_printf(mon, "postcopy request count: %" PRIu64 "\n",
                           info->ram->postcopy_requests);
        }
    }

    if (info->has_disk) {
        monitor_printf(mon, "transferred disk: %" PRIu64 " kbytes\n",
                       info->disk->transferred >> 10);
        monitor_printf(mon, "remaining disk: %" PRIu64 " kbytes\n",
                       info->disk->remaining >> 10);
        monitor_printf(mon, "total disk: %" PRIu64 " kbytes\n",
                       info->disk->total >> 10);
    }

    if (info->has_xbzrle_cache) {
        monitor_printf(mon, "cache size: %" PRIu64 " bytes\n",
                       info->xbzrle_cache->cache_size);
        monitor_printf(mon, "xbzrle transferred: %" PRIu64 " kbytes\n",
                       info->xbzrle_cache->bytes >> 10);
        monitor_printf(mon, "xbzrle pages: %" PRIu64 " pages\n",
                       info->xbzrle_cache->pages);
        monitor_printf(mon, "xbzrle cache miss: %" PRIu64 "\n",
                       info->xbzrle_cache->cache_miss);
        monitor_printf(mon, "xbzrle cache miss rate: %0.2f\n",
                       info->xbzrle_cache->cache_miss_rate);
        monitor_printf(mon, "xbzrle overflow : %" PRIu64 "\n",
                       info->xbzrle_cache->overflow);
    }

    if (info->has_cpu_throttle_percentage) {
        monitor_printf(mon, "cpu throttle percentage: %" PRIu64 "\n",
                       info->cpu_throttle_percentage);
    }

    qapi_free_MigrationInfo(info);
    qapi_free_MigrationCapabilityStatusList(caps);
}

void hmp_info_migrate_capabilities(Monitor *mon, const QDict *qdict)
{
    MigrationCapabilityStatusList *caps, *cap;

    caps = qmp_query_migrate_capabilities(NULL);

    if (caps) {
        for (cap = caps; cap; cap = cap->next) {
            monitor_printf(mon, "%s: %s\n",
                           MigrationCapability_lookup[cap->value->capability],
                           cap->value->state ? "on" : "off");
        }
    }

    qapi_free_MigrationCapabilityStatusList(caps);
}

void hmp_info_migrate_parameters(Monitor *mon, const QDict *qdict)
{
    MigrationParameters *params;

    params = qmp_query_migrate_parameters(NULL);

    if (params) {
        assert(params->has_compress_level);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_COMPRESS_LEVEL],
            params->compress_level);
        assert(params->has_compress_threads);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_COMPRESS_THREADS],
            params->compress_threads);
        assert(params->has_decompress_threads);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_DECOMPRESS_THREADS],
            params->decompress_threads);
        assert(params->has_cpu_throttle_initial);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_CPU_THROTTLE_INITIAL],
            params->cpu_throttle_initial);
        assert(params->has_cpu_throttle_increment);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_CPU_THROTTLE_INCREMENT],
            params->cpu_throttle_increment);
        assert(params->has_tls_creds);
        monitor_printf(mon, "%s: '%s'\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_TLS_CREDS],
            params->tls_creds);
        assert(params->has_tls_hostname);
        monitor_printf(mon, "%s: '%s'\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_TLS_HOSTNAME],
            params->tls_hostname);
        assert(params->has_max_bandwidth);
        monitor_printf(mon, "%s: %" PRId64 " bytes/second\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_MAX_BANDWIDTH],
            params->max_bandwidth);
        assert(params->has_downtime_limit);
        monitor_printf(mon, "%s: %" PRId64 " milliseconds\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_DOWNTIME_LIMIT],
            params->downtime_limit);
        assert(params->has_x_checkpoint_delay);
        monitor_printf(mon, "%s: %" PRId64 "\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_X_CHECKPOINT_DELAY],
            params->x_checkpoint_delay);
        assert(params->has_block_incremental);
        monitor_printf(mon, "%s: %s\n",
            MigrationParameter_lookup[MIGRATION_PARAMETER_BLOCK_INCREMENTAL],
                       params->block_incremental ? "on" : "off");
    }

    qapi_free_MigrationParameters(params);
}

void hmp_info_migrate_cache_size(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "xbzrel cache size: %" PRId64 " kbytes\n",
                   qmp_query_migrate_cache_size(NULL) >> 10);
}

void hmp_info_cpus(Monitor *mon, const QDict *qdict)
{
    CpuInfoList *cpu_list, *cpu;

    cpu_list = qmp_query_cpus(NULL);

    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        int active = ' ';

        if (cpu->value->CPU == monitor_get_cpu_index()) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ":", active, cpu->value->CPU);

        switch (cpu->value->arch) {
        case CPU_INFO_ARCH_X86:
            monitor_printf(mon, " pc=0x%016" PRIx64, cpu->value->u.x86.pc);
            break;
        case CPU_INFO_ARCH_PPC:
            monitor_printf(mon, " nip=0x%016" PRIx64, cpu->value->u.ppc.nip);
            break;
        case CPU_INFO_ARCH_SPARC:
            monitor_printf(mon, " pc=0x%016" PRIx64,
                           cpu->value->u.q_sparc.pc);
            monitor_printf(mon, " npc=0x%016" PRIx64,
                           cpu->value->u.q_sparc.npc);
            break;
        case CPU_INFO_ARCH_MIPS:
            monitor_printf(mon, " PC=0x%016" PRIx64, cpu->value->u.q_mips.PC);
            break;
        case CPU_INFO_ARCH_TRICORE:
            monitor_printf(mon, " PC=0x%016" PRIx64, cpu->value->u.tricore.PC);
            break;
        default:
            break;
        }

        if (cpu->value->halted) {
            monitor_printf(mon, " (halted)");
        }

        monitor_printf(mon, " thread_id=%" PRId64 "\n", cpu->value->thread_id);
    }

    qapi_free_CpuInfoList(cpu_list);
}

static void print_block_info(Monitor *mon, BlockInfo *info,
                             BlockDeviceInfo *inserted, bool verbose)
{
    ImageInfo *image_info;

    assert(!info || !info->has_inserted || info->inserted == inserted);

    if (info && *info->device) {
        monitor_printf(mon, "%s", info->device);
        if (inserted && inserted->has_node_name) {
            monitor_printf(mon, " (%s)", inserted->node_name);
        }
    } else {
        assert(info || inserted);
        monitor_printf(mon, "%s",
                       inserted && inserted->has_node_name ? inserted->node_name
                       : info && info->has_qdev ? info->qdev
                       : "<anonymous>");
    }

    if (inserted) {
        monitor_printf(mon, ": %s (%s%s%s)\n",
                       inserted->file,
                       inserted->drv,
                       inserted->ro ? ", read-only" : "",
                       inserted->encrypted ? ", encrypted" : "");
    } else {
        monitor_printf(mon, ": [not inserted]\n");
    }

    if (info) {
        if (info->has_qdev) {
            monitor_printf(mon, "    Attached to:      %s\n", info->qdev);
        }
        if (info->has_io_status && info->io_status != BLOCK_DEVICE_IO_STATUS_OK) {
            monitor_printf(mon, "    I/O status:       %s\n",
                           BlockDeviceIoStatus_lookup[info->io_status]);
        }

        if (info->removable) {
            monitor_printf(mon, "    Removable device: %slocked, tray %s\n",
                           info->locked ? "" : "not ",
                           info->tray_open ? "open" : "closed");
        }
    }


    if (!inserted) {
        return;
    }

    monitor_printf(mon, "    Cache mode:       %s%s%s\n",
                   inserted->cache->writeback ? "writeback" : "writethrough",
                   inserted->cache->direct ? ", direct" : "",
                   inserted->cache->no_flush ? ", ignore flushes" : "");

    if (inserted->has_backing_file) {
        monitor_printf(mon,
                       "    Backing file:     %s "
                       "(chain depth: %" PRId64 ")\n",
                       inserted->backing_file,
                       inserted->backing_file_depth);
    }

    if (inserted->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF) {
        monitor_printf(mon, "    Detect zeroes:    %s\n",
                       BlockdevDetectZeroesOptions_lookup[inserted->detect_zeroes]);
    }

    if (inserted->bps  || inserted->bps_rd  || inserted->bps_wr  ||
        inserted->iops || inserted->iops_rd || inserted->iops_wr)
    {
        monitor_printf(mon, "    I/O throttling:   bps=%" PRId64
                        " bps_rd=%" PRId64  " bps_wr=%" PRId64
                        " bps_max=%" PRId64
                        " bps_rd_max=%" PRId64
                        " bps_wr_max=%" PRId64
                        " iops=%" PRId64 " iops_rd=%" PRId64
                        " iops_wr=%" PRId64
                        " iops_max=%" PRId64
                        " iops_rd_max=%" PRId64
                        " iops_wr_max=%" PRId64
                        " iops_size=%" PRId64
                        " group=%s\n",
                        inserted->bps,
                        inserted->bps_rd,
                        inserted->bps_wr,
                        inserted->bps_max,
                        inserted->bps_rd_max,
                        inserted->bps_wr_max,
                        inserted->iops,
                        inserted->iops_rd,
                        inserted->iops_wr,
                        inserted->iops_max,
                        inserted->iops_rd_max,
                        inserted->iops_wr_max,
                        inserted->iops_size,
                        inserted->group);
    }

    if (verbose) {
        monitor_printf(mon, "\nImages:\n");
        image_info = inserted->image;
        while (1) {
                bdrv_image_info_dump((fprintf_function)monitor_printf,
                                     mon, image_info);
            if (image_info->has_backing_image) {
                image_info = image_info->backing_image;
            } else {
                break;
            }
        }
    }
}

void hmp_info_block(Monitor *mon, const QDict *qdict)
{
    BlockInfoList *block_list, *info;
    BlockDeviceInfoList *blockdev_list, *blockdev;
    const char *device = qdict_get_try_str(qdict, "device");
    bool verbose = qdict_get_try_bool(qdict, "verbose", false);
    bool nodes = qdict_get_try_bool(qdict, "nodes", false);
    bool printed = false;

    /* Print BlockBackend information */
    if (!nodes) {
        block_list = qmp_query_block(NULL);
    } else {
        block_list = NULL;
    }

    for (info = block_list; info; info = info->next) {
        if (device && strcmp(device, info->value->device)) {
            continue;
        }

        if (info != block_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, info->value, info->value->has_inserted
                                           ? info->value->inserted : NULL,
                         verbose);
        printed = true;
    }

    qapi_free_BlockInfoList(block_list);

    if ((!device && !nodes) || printed) {
        return;
    }

    /* Print node information */
    blockdev_list = qmp_query_named_block_nodes(NULL);
    for (blockdev = blockdev_list; blockdev; blockdev = blockdev->next) {
        assert(blockdev->value->has_node_name);
        if (device && strcmp(device, blockdev->value->node_name)) {
            continue;
        }

        if (blockdev != blockdev_list) {
            monitor_printf(mon, "\n");
        }

        print_block_info(mon, NULL, blockdev->value, verbose);
    }
    qapi_free_BlockDeviceInfoList(blockdev_list);
}

void hmp_info_blockstats(Monitor *mon, const QDict *qdict)
{
    BlockStatsList *stats_list, *stats;

    stats_list = qmp_query_blockstats(false, false, NULL);

    for (stats = stats_list; stats; stats = stats->next) {
        if (!stats->value->has_device) {
            continue;
        }

        monitor_printf(mon, "%s:", stats->value->device);
        monitor_printf(mon, " rd_bytes=%" PRId64
                       " wr_bytes=%" PRId64
                       " rd_operations=%" PRId64
                       " wr_operations=%" PRId64
                       " flush_operations=%" PRId64
                       " wr_total_time_ns=%" PRId64
                       " rd_total_time_ns=%" PRId64
                       " flush_total_time_ns=%" PRId64
                       " rd_merged=%" PRId64
                       " wr_merged=%" PRId64
                       " idle_time_ns=%" PRId64
                       "\n",
                       stats->value->stats->rd_bytes,
                       stats->value->stats->wr_bytes,
                       stats->value->stats->rd_operations,
                       stats->value->stats->wr_operations,
                       stats->value->stats->flush_operations,
                       stats->value->stats->wr_total_time_ns,
                       stats->value->stats->rd_total_time_ns,
                       stats->value->stats->flush_total_time_ns,
                       stats->value->stats->rd_merged,
                       stats->value->stats->wr_merged,
                       stats->value->stats->idle_time_ns);
    }

    qapi_free_BlockStatsList(stats_list);
}

/* Helper for hmp_info_vnc_clients, _servers */
static void hmp_info_VncBasicInfo(Monitor *mon, VncBasicInfo *info,
                                  const char *name)
{
    monitor_printf(mon, "  %s: %s:%s (%s%s)\n",
                   name,
                   info->host,
                   info->service,
                   NetworkAddressFamily_lookup[info->family],
                   info->websocket ? " (Websocket)" : "");
}

/* Helper displaying and auth and crypt info */
static void hmp_info_vnc_authcrypt(Monitor *mon, const char *indent,
                                   VncPrimaryAuth auth,
                                   VncVencryptSubAuth *vencrypt)
{
    monitor_printf(mon, "%sAuth: %s (Sub: %s)\n", indent,
                   VncPrimaryAuth_lookup[auth],
                   vencrypt ? VncVencryptSubAuth_lookup[*vencrypt] : "none");
}

static void hmp_info_vnc_clients(Monitor *mon, VncClientInfoList *client)
{
    while (client) {
        VncClientInfo *cinfo = client->value;

        hmp_info_VncBasicInfo(mon, qapi_VncClientInfo_base(cinfo), "Client");
        monitor_printf(mon, "    x509_dname: %s\n",
                       cinfo->has_x509_dname ?
                       cinfo->x509_dname : "none");
        monitor_printf(mon, "    sasl_username: %s\n",
                       cinfo->has_sasl_username ?
                       cinfo->sasl_username : "none");

        client = client->next;
    }
}

static void hmp_info_vnc_servers(Monitor *mon, VncServerInfo2List *server)
{
    while (server) {
        VncServerInfo2 *sinfo = server->value;
        hmp_info_VncBasicInfo(mon, qapi_VncServerInfo2_base(sinfo), "Server");
        hmp_info_vnc_authcrypt(mon, "    ", sinfo->auth,
                               sinfo->has_vencrypt ? &sinfo->vencrypt : NULL);
        server = server->next;
    }
}

void hmp_info_vnc(Monitor *mon, const QDict *qdict)
{
    VncInfo2List *info2l;
    Error *err = NULL;

    info2l = qmp_query_vnc_servers(&err);
    if (err) {
        error_report_err(err);
        return;
    }
    if (!info2l) {
        monitor_printf(mon, "None\n");
        return;
    }

    while (info2l) {
        VncInfo2 *info = info2l->value;
        monitor_printf(mon, "%s:\n", info->id);
        hmp_info_vnc_servers(mon, info->server);
        hmp_info_vnc_clients(mon, info->clients);
        if (!info->server) {
            /* The server entry displays its auth, we only
             * need to display in the case of 'reverse' connections
             * where there's no server.
             */
            hmp_info_vnc_authcrypt(mon, "  ", info->auth,
                               info->has_vencrypt ? &info->vencrypt : NULL);
        }
        if (info->has_display) {
            monitor_printf(mon, "  Display: %s\n", info->display);
        }
        info2l = info2l->next;
    }

    qapi_free_VncInfo2List(info2l);

}

#ifdef CONFIG_SPICE
void hmp_info_spice(Monitor *mon, const QDict *qdict)
{
    SpiceChannelList *chan;
    SpiceInfo *info;
    const char *channel_name;
    const char * const channel_names[] = {
        [SPICE_CHANNEL_MAIN] = "main",
        [SPICE_CHANNEL_DISPLAY] = "display",
        [SPICE_CHANNEL_INPUTS] = "inputs",
        [SPICE_CHANNEL_CURSOR] = "cursor",
        [SPICE_CHANNEL_PLAYBACK] = "playback",
        [SPICE_CHANNEL_RECORD] = "record",
        [SPICE_CHANNEL_TUNNEL] = "tunnel",
        [SPICE_CHANNEL_SMARTCARD] = "smartcard",
        [SPICE_CHANNEL_USBREDIR] = "usbredir",
        [SPICE_CHANNEL_PORT] = "port",
#if 0
        /* minimum spice-protocol is 0.12.3, webdav was added in 0.12.7,
         * no easy way to #ifdef (SPICE_CHANNEL_* is a enum).  Disable
         * as quick fix for build failures with older versions. */
        [SPICE_CHANNEL_WEBDAV] = "webdav",
#endif
    };

    info = qmp_query_spice(NULL);

    if (!info->enabled) {
        monitor_printf(mon, "Server: disabled\n");
        goto out;
    }

    monitor_printf(mon, "Server:\n");
    if (info->has_port) {
        monitor_printf(mon, "     address: %s:%" PRId64 "\n",
                       info->host, info->port);
    }
    if (info->has_tls_port) {
        monitor_printf(mon, "     address: %s:%" PRId64 " [tls]\n",
                       info->host, info->tls_port);
    }
    monitor_printf(mon, "    migrated: %s\n",
                   info->migrated ? "true" : "false");
    monitor_printf(mon, "        auth: %s\n", info->auth);
    monitor_printf(mon, "    compiled: %s\n", info->compiled_version);
    monitor_printf(mon, "  mouse-mode: %s\n",
                   SpiceQueryMouseMode_lookup[info->mouse_mode]);

    if (!info->has_channels || info->channels == NULL) {
        monitor_printf(mon, "Channels: none\n");
    } else {
        for (chan = info->channels; chan; chan = chan->next) {
            monitor_printf(mon, "Channel:\n");
            monitor_printf(mon, "     address: %s:%s%s\n",
                           chan->value->host, chan->value->port,
                           chan->value->tls ? " [tls]" : "");
            monitor_printf(mon, "     session: %" PRId64 "\n",
                           chan->value->connection_id);
            monitor_printf(mon, "     channel: %" PRId64 ":%" PRId64 "\n",
                           chan->value->channel_type, chan->value->channel_id);

            channel_name = "unknown";
            if (chan->value->channel_type > 0 &&
                chan->value->channel_type < ARRAY_SIZE(channel_names) &&
                channel_names[chan->value->channel_type]) {
                channel_name = channel_names[chan->value->channel_type];
            }

            monitor_printf(mon, "     channel name: %s\n", channel_name);
        }
    }

out:
    qapi_free_SpiceInfo(info);
}
#endif

void hmp_info_balloon(Monitor *mon, const QDict *qdict)
{
    BalloonInfo *info;
    Error *err = NULL;

    info = qmp_query_balloon(&err);
    if (err) {
        error_report_err(err);
        return;
    }

    monitor_printf(mon, "balloon: actual=%" PRId64 "\n", info->actual >> 20);

    qapi_free_BalloonInfo(info);
}

static void hmp_info_pci_device(Monitor *mon, const PciDeviceInfo *dev)
{
    PciMemoryRegionList *region;

    monitor_printf(mon, "  Bus %2" PRId64 ", ", dev->bus);
    monitor_printf(mon, "device %3" PRId64 ", function %" PRId64 ":\n",
                   dev->slot, dev->function);
    monitor_printf(mon, "    ");

    if (dev->class_info->has_desc) {
        monitor_printf(mon, "%s", dev->class_info->desc);
    } else {
        monitor_printf(mon, "Class %04" PRId64, dev->class_info->q_class);
    }

    monitor_printf(mon, ": PCI device %04" PRIx64 ":%04" PRIx64 "\n",
                   dev->id->vendor, dev->id->device);

    if (dev->has_irq) {
        monitor_printf(mon, "      IRQ %" PRId64 ".\n", dev->irq);
    }

    if (dev->has_pci_bridge) {
        monitor_printf(mon, "      BUS %" PRId64 ".\n",
                       dev->pci_bridge->bus->number);
        monitor_printf(mon, "      secondary bus %" PRId64 ".\n",
                       dev->pci_bridge->bus->secondary);
        monitor_printf(mon, "      subordinate bus %" PRId64 ".\n",
                       dev->pci_bridge->bus->subordinate);

        monitor_printf(mon, "      IO range [0x%04"PRIx64", 0x%04"PRIx64"]\n",
                       dev->pci_bridge->bus->io_range->base,
                       dev->pci_bridge->bus->io_range->limit);

        monitor_printf(mon,
                       "      memory range [0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus->memory_range->base,
                       dev->pci_bridge->bus->memory_range->limit);

        monitor_printf(mon, "      prefetchable memory range "
                       "[0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus->prefetchable_range->base,
                       dev->pci_bridge->bus->prefetchable_range->limit);
    }

    for (region = dev->regions; region; region = region->next) {
        uint64_t addr, size;

        addr = region->value->address;
        size = region->value->size;

        monitor_printf(mon, "      BAR%" PRId64 ": ", region->value->bar);

        if (!strcmp(region->value->type, "io")) {
            monitor_printf(mon, "I/O at 0x%04" PRIx64
                                " [0x%04" PRIx64 "].\n",
                           addr, addr + size - 1);
        } else {
            monitor_printf(mon, "%d bit%s memory at 0x%08" PRIx64
                               " [0x%08" PRIx64 "].\n",
                           region->value->mem_type_64 ? 64 : 32,
                           region->value->prefetch ? " prefetchable" : "",
                           addr, addr + size - 1);
        }
    }

    monitor_printf(mon, "      id \"%s\"\n", dev->qdev_id);

    if (dev->has_pci_bridge) {
        if (dev->pci_bridge->has_devices) {
            PciDeviceInfoList *cdev;
            for (cdev = dev->pci_bridge->devices; cdev; cdev = cdev->next) {
                hmp_info_pci_device(mon, cdev->value);
            }
        }
    }
}

static int hmp_info_irq_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    Monitor *mon = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        uint64_t *irq_counts;
        unsigned int nb_irqs, i;
        if (k->get_statistics &&
            k->get_statistics(intc, &irq_counts, &nb_irqs)) {
            if (nb_irqs > 0) {
                monitor_printf(mon, "IRQ statistics for %s:\n",
                               object_get_typename(obj));
                for (i = 0; i < nb_irqs; i++) {
                    if (irq_counts[i] > 0) {
                        monitor_printf(mon, "%2d: %" PRId64 "\n", i,
                                       irq_counts[i]);
                    }
                }
            }
        } else {
            monitor_printf(mon, "IRQ statistics not available for %s.\n",
                           object_get_typename(obj));
        }
    }

    return 0;
}

void hmp_info_irq(Monitor *mon, const QDict *qdict)
{
    object_child_foreach_recursive(object_get_root(),
                                   hmp_info_irq_foreach, mon);
}

static int hmp_info_pic_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    Monitor *mon = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        if (k->print_info) {
            k->print_info(intc, mon);
        } else {
            monitor_printf(mon, "Interrupt controller information not available for %s.\n",
                           object_get_typename(obj));
        }
    }

    return 0;
}

void hmp_info_pic(Monitor *mon, const QDict *qdict)
{
    object_child_foreach_recursive(object_get_root(),
                                   hmp_info_pic_foreach, mon);
}

void hmp_info_pci(Monitor *mon, const QDict *qdict)
{
    PciInfoList *info_list, *info;
    Error *err = NULL;

    info_list = qmp_query_pci(&err);
    if (err) {
        monitor_printf(mon, "PCI devices not supported\n");
        error_free(err);
        return;
    }

    for (info = info_list; info; info = info->next) {
        PciDeviceInfoList *dev;

        for (dev = info->value->devices; dev; dev = dev->next) {
            hmp_info_pci_device(mon, dev->value);
        }
    }

    qapi_free_PciInfoList(info_list);
}

void hmp_info_block_jobs(Monitor *mon, const QDict *qdict)
{
    BlockJobInfoList *list;
    Error *err = NULL;

    list = qmp_query_block_jobs(&err);
    assert(!err);

    if (!list) {
        monitor_printf(mon, "No active jobs\n");
        return;
    }

    while (list) {
        if (strcmp(list->value->type, "stream") == 0) {
            monitor_printf(mon, "Streaming device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        } else {
            monitor_printf(mon, "Type %s, device %s: Completed %" PRId64
                           " of %" PRId64 " bytes, speed limit %" PRId64
                           " bytes/s\n",
                           list->value->type,
                           list->value->device,
                           list->value->offset,
                           list->value->len,
                           list->value->speed);
        }
        list = list->next;
    }

    qapi_free_BlockJobInfoList(list);
}

void hmp_info_tpm(Monitor *mon, const QDict *qdict)
{
    TPMInfoList *info_list, *info;
    Error *err = NULL;
    unsigned int c = 0;
    TPMPassthroughOptions *tpo;

    info_list = qmp_query_tpm(&err);
    if (err) {
        monitor_printf(mon, "TPM device not supported\n");
        error_free(err);
        return;
    }

    if (info_list) {
        monitor_printf(mon, "TPM device:\n");
    }

    for (info = info_list; info; info = info->next) {
        TPMInfo *ti = info->value;
        monitor_printf(mon, " tpm%d: model=%s\n",
                       c, TpmModel_lookup[ti->model]);

        monitor_printf(mon, "  \\ %s: type=%s",
                       ti->id, TpmTypeOptionsKind_lookup[ti->options->type]);

        switch (ti->options->type) {
        case TPM_TYPE_OPTIONS_KIND_PASSTHROUGH:
            tpo = ti->options->u.passthrough.data;
            monitor_printf(mon, "%s%s%s%s",
                           tpo->has_path ? ",path=" : "",
                           tpo->has_path ? tpo->path : "",
                           tpo->has_cancel_path ? ",cancel-path=" : "",
                           tpo->has_cancel_path ? tpo->cancel_path : "");
            break;
        case TPM_TYPE_OPTIONS_KIND__MAX:
            break;
        }
        monitor_printf(mon, "\n");
        c++;
    }
    qapi_free_TPMInfoList(info_list);
}

void hmp_quit(Monitor *mon, const QDict *qdict)
{
    monitor_suspend(mon);
    qmp_quit(NULL);
}

void hmp_stop(Monitor *mon, const QDict *qdict)
{
    qmp_stop(NULL);
}

void hmp_system_reset(Monitor *mon, const QDict *qdict)
{
    qmp_system_reset(NULL);
}

void hmp_system_powerdown(Monitor *mon, const QDict *qdict)
{
    qmp_system_powerdown(NULL);
}

void hmp_cpu(Monitor *mon, const QDict *qdict)
{
    int64_t cpu_index;

    /* XXX: drop the monitor_set_cpu() usage when all HMP commands that
            use it are converted to the QAPI */
    cpu_index = qdict_get_int(qdict, "index");
    if (monitor_set_cpu(cpu_index) < 0) {
        monitor_printf(mon, "invalid CPU index\n");
    }
}

void hmp_memsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;
    int cpu_index = monitor_get_cpu_index();

    if (cpu_index < 0) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    qmp_memsave(addr, size, filename, true, cpu_index, &err);
    hmp_handle_error(mon, &err);
}

void hmp_pmemsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;

    qmp_pmemsave(addr, size, filename, &err);
    hmp_handle_error(mon, &err);
}

void hmp_ringbuf_write(Monitor *mon, const QDict *qdict)
{
    const char *chardev = qdict_get_str(qdict, "device");
    const char *data = qdict_get_str(qdict, "data");
    Error *err = NULL;

    qmp_ringbuf_write(chardev, data, false, 0, &err);

    hmp_handle_error(mon, &err);
}

void hmp_ringbuf_read(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *chardev = qdict_get_str(qdict, "device");
    char *data;
    Error *err = NULL;
    int i;

    data = qmp_ringbuf_read(chardev, size, false, 0, &err);
    if (err) {
        error_report_err(err);
        return;
    }

    for (i = 0; data[i]; i++) {
        unsigned char ch = data[i];

        if (ch == '\\') {
            monitor_printf(mon, "\\\\");
        } else if ((ch < 0x20 && ch != '\n' && ch != '\t') || ch == 0x7F) {
            monitor_printf(mon, "\\u%04X", ch);
        } else {
            monitor_printf(mon, "%c", ch);
        }

    }
    monitor_printf(mon, "\n");
    g_free(data);
}

void hmp_cont(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_cont(&err);
    hmp_handle_error(mon, &err);
}

void hmp_system_wakeup(Monitor *mon, const QDict *qdict)
{
    qmp_system_wakeup(NULL);
}

void hmp_nmi(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_inject_nmi(&err);
    hmp_handle_error(mon, &err);
}

void hmp_set_link(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    bool up = qdict_get_bool(qdict, "up");
    Error *err = NULL;

    qmp_set_link(name, up, &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_block_passwd(true, device, false, NULL, password, &err);
    hmp_handle_error(mon, &err);
}

void hmp_balloon(Monitor *mon, const QDict *qdict)
{
    int64_t value = qdict_get_int(qdict, "value");
    Error *err = NULL;

    qmp_balloon(value, &err);
    if (err) {
        error_report_err(err);
    }
}

void hmp_block_resize(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    int64_t size = qdict_get_int(qdict, "size");
    Error *err = NULL;

    qmp_block_resize(true, device, false, NULL, size, &err);
    hmp_handle_error(mon, &err);
}

void hmp_drive_mirror(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    Error *err = NULL;
    DriveMirror mirror = {
        .device = (char *)qdict_get_str(qdict, "device"),
        .target = (char *)filename,
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .unmap = true,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, &err);
        return;
    }
    qmp_drive_mirror(&mirror, &err);
    hmp_handle_error(mon, &err);
}

void hmp_drive_backup(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    bool full = qdict_get_try_bool(qdict, "full", false);
    bool compress = qdict_get_try_bool(qdict, "compress", false);
    Error *err = NULL;
    DriveBackup backup = {
        .device = (char *)device,
        .target = (char *)filename,
        .has_format = !!format,
        .format = (char *)format,
        .sync = full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
        .has_mode = true,
        .mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS,
        .has_compress = !!compress,
        .compress = compress,
    };

    if (!filename) {
        error_setg(&err, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, &err);
        return;
    }

    qmp_drive_backup(&backup, &err);
    hmp_handle_error(mon, &err);
}

void hmp_snapshot_blkdev(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_try_str(qdict, "snapshot-file");
    const char *format = qdict_get_try_str(qdict, "format");
    bool reuse = qdict_get_try_bool(qdict, "reuse", false);
    enum NewImageMode mode;
    Error *err = NULL;

    if (!filename) {
        /* In the future, if 'snapshot-file' is not specified, the snapshot
           will be taken internally. Today it's actually required. */
        error_setg(&err, QERR_MISSING_PARAMETER, "snapshot-file");
        hmp_handle_error(mon, &err);
        return;
    }

    mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    qmp_blockdev_snapshot_sync(true, device, false, NULL,
                               filename, false, NULL,
                               !!format, format,
                               true, mode, &err);
    hmp_handle_error(mon, &err);
}

void hmp_snapshot_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    qmp_blockdev_snapshot_internal_sync(device, name, &err);
    hmp_handle_error(mon, &err);
}

void hmp_snapshot_delete_blkdev_internal(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *name = qdict_get_str(qdict, "name");
    const char *id = qdict_get_try_str(qdict, "id");
    Error *err = NULL;

    qmp_blockdev_snapshot_delete_internal_sync(device, !!id, id,
                                               true, name, &err);
    hmp_handle_error(mon, &err);
}

void hmp_loadvm(Monitor *mon, const QDict *qdict)
{
    int saved_vm_running  = runstate_is_running();
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    vm_stop(RUN_STATE_RESTORE_VM);

    if (load_snapshot(name, &err) == 0 && saved_vm_running) {
        vm_start();
    }
    hmp_handle_error(mon, &err);
}

void hmp_savevm(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    save_snapshot(qdict_get_try_str(qdict, "name"), &err);
    hmp_handle_error(mon, &err);
}

void hmp_delvm(Monitor *mon, const QDict *qdict)
{
    BlockDriverState *bs;
    Error *err;
    const char *name = qdict_get_str(qdict, "name");

    if (bdrv_all_delete_snapshot(name, &bs, &err) < 0) {
        error_reportf_err(err,
                          "Error while deleting snapshot on device '%s': ",
                          bdrv_get_device_name(bs));
    }
}

void hmp_info_snapshots(Monitor *mon, const QDict *qdict)
{
    BlockDriverState *bs, *bs1;
    BdrvNextIterator it1;
    QEMUSnapshotInfo *sn_tab, *sn;
    bool no_snapshot = true;
    int nb_sns, i;
    int total;
    int *global_snapshots;
    AioContext *aio_context;

    typedef struct SnapshotEntry {
        QEMUSnapshotInfo sn;
        QTAILQ_ENTRY(SnapshotEntry) next;
    } SnapshotEntry;

    typedef struct ImageEntry {
        const char *imagename;
        QTAILQ_ENTRY(ImageEntry) next;
        QTAILQ_HEAD(, SnapshotEntry) snapshots;
    } ImageEntry;

    QTAILQ_HEAD(, ImageEntry) image_list =
        QTAILQ_HEAD_INITIALIZER(image_list);

    ImageEntry *image_entry, *next_ie;
    SnapshotEntry *snapshot_entry;

    bs = bdrv_all_find_vmstate_bs();
    if (!bs) {
        monitor_printf(mon, "No available block device supports snapshots\n");
        return;
    }
    aio_context = bdrv_get_aio_context(bs);

    aio_context_acquire(aio_context);
    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    aio_context_release(aio_context);

    if (nb_sns < 0) {
        monitor_printf(mon, "bdrv_snapshot_list: error %d\n", nb_sns);
        return;
    }

    for (bs1 = bdrv_first(&it1); bs1; bs1 = bdrv_next(&it1)) {
        int bs1_nb_sns = 0;
        ImageEntry *ie;
        SnapshotEntry *se;
        AioContext *ctx = bdrv_get_aio_context(bs1);

        aio_context_acquire(ctx);
        if (bdrv_can_snapshot(bs1)) {
            sn = NULL;
            bs1_nb_sns = bdrv_snapshot_list(bs1, &sn);
            if (bs1_nb_sns > 0) {
                no_snapshot = false;
                ie = g_new0(ImageEntry, 1);
                ie->imagename = bdrv_get_device_name(bs1);
                QTAILQ_INIT(&ie->snapshots);
                QTAILQ_INSERT_TAIL(&image_list, ie, next);
                for (i = 0; i < bs1_nb_sns; i++) {
                    se = g_new0(SnapshotEntry, 1);
                    se->sn = sn[i];
                    QTAILQ_INSERT_TAIL(&ie->snapshots, se, next);
                }
            }
            g_free(sn);
        }
        aio_context_release(ctx);
    }

    if (no_snapshot) {
        monitor_printf(mon, "There is no snapshot available.\n");
        return;
    }

    global_snapshots = g_new0(int, nb_sns);
    total = 0;
    for (i = 0; i < nb_sns; i++) {
        SnapshotEntry *next_sn;
        if (bdrv_all_find_snapshot(sn_tab[i].name, &bs1) == 0) {
            global_snapshots[total] = i;
            total++;
            QTAILQ_FOREACH(image_entry, &image_list, next) {
                QTAILQ_FOREACH_SAFE(snapshot_entry, &image_entry->snapshots,
                                    next, next_sn) {
                    if (!strcmp(sn_tab[i].name, snapshot_entry->sn.name)) {
                        QTAILQ_REMOVE(&image_entry->snapshots, snapshot_entry,
                                      next);
                        g_free(snapshot_entry);
                    }
                }
            }
        }
    }

    monitor_printf(mon, "List of snapshots present on all disks:\n");

    if (total > 0) {
        bdrv_snapshot_dump((fprintf_function)monitor_printf, mon, NULL);
        monitor_printf(mon, "\n");
        for (i = 0; i < total; i++) {
            sn = &sn_tab[global_snapshots[i]];
            /* The ID is not guaranteed to be the same on all images, so
             * overwrite it.
             */
            pstrcpy(sn->id_str, sizeof(sn->id_str), "--");
            bdrv_snapshot_dump((fprintf_function)monitor_printf, mon, sn);
            monitor_printf(mon, "\n");
        }
    } else {
        monitor_printf(mon, "None\n");
    }

    QTAILQ_FOREACH(image_entry, &image_list, next) {
        if (QTAILQ_EMPTY(&image_entry->snapshots)) {
            continue;
        }
        monitor_printf(mon,
                       "\nList of partial (non-loadable) snapshots on '%s':\n",
                       image_entry->imagename);
        bdrv_snapshot_dump((fprintf_function)monitor_printf, mon, NULL);
        monitor_printf(mon, "\n");
        QTAILQ_FOREACH(snapshot_entry, &image_entry->snapshots, next) {
            bdrv_snapshot_dump((fprintf_function)monitor_printf, mon,
                               &snapshot_entry->sn);
            monitor_printf(mon, "\n");
        }
    }

    QTAILQ_FOREACH_SAFE(image_entry, &image_list, next, next_ie) {
        SnapshotEntry *next_sn;
        QTAILQ_FOREACH_SAFE(snapshot_entry, &image_entry->snapshots, next,
                            next_sn) {
            g_free(snapshot_entry);
        }
        g_free(image_entry);
    }
    g_free(sn_tab);
    g_free(global_snapshots);

}

void hmp_migrate_cancel(Monitor *mon, const QDict *qdict)
{
    qmp_migrate_cancel(NULL);
}

void hmp_migrate_incoming(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *uri = qdict_get_str(qdict, "uri");

    qmp_migrate_incoming(uri, &err);

    hmp_handle_error(mon, &err);
}

/* Kept for backwards compatibility */
void hmp_migrate_set_downtime(Monitor *mon, const QDict *qdict)
{
    double value = qdict_get_double(qdict, "value");
    qmp_migrate_set_downtime(value, NULL);
}

void hmp_migrate_set_cache_size(Monitor *mon, const QDict *qdict)
{
    int64_t value = qdict_get_int(qdict, "value");
    Error *err = NULL;

    qmp_migrate_set_cache_size(value, &err);
    if (err) {
        error_report_err(err);
        return;
    }
}

/* Kept for backwards compatibility */
void hmp_migrate_set_speed(Monitor *mon, const QDict *qdict)
{
    int64_t value = qdict_get_int(qdict, "value");
    qmp_migrate_set_speed(value, NULL);
}

void hmp_migrate_set_capability(Monitor *mon, const QDict *qdict)
{
    const char *cap = qdict_get_str(qdict, "capability");
    bool state = qdict_get_bool(qdict, "state");
    Error *err = NULL;
    MigrationCapabilityStatusList *caps = g_malloc0(sizeof(*caps));
    int i;

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (strcmp(cap, MigrationCapability_lookup[i]) == 0) {
            caps->value = g_malloc0(sizeof(*caps->value));
            caps->value->capability = i;
            caps->value->state = state;
            caps->next = NULL;
            qmp_migrate_set_capabilities(caps, &err);
            break;
        }
    }

    if (i == MIGRATION_CAPABILITY__MAX) {
        error_setg(&err, QERR_INVALID_PARAMETER, cap);
    }

    qapi_free_MigrationCapabilityStatusList(caps);

    if (err) {
        error_report_err(err);
    }
}

void hmp_migrate_set_parameter(Monitor *mon, const QDict *qdict)
{
    const char *param = qdict_get_str(qdict, "parameter");
    const char *valuestr = qdict_get_str(qdict, "value");
    Visitor *v = string_input_visitor_new(valuestr);
    MigrateSetParameters *p = g_new0(MigrateSetParameters, 1);
    uint64_t valuebw = 0;
    Error *err = NULL;
    int i, ret;

    for (i = 0; i < MIGRATION_PARAMETER__MAX; i++) {
        if (strcmp(param, MigrationParameter_lookup[i]) == 0) {
            switch (i) {
            case MIGRATION_PARAMETER_COMPRESS_LEVEL:
                p->has_compress_level = true;
                visit_type_int(v, param, &p->compress_level, &err);
                break;
            case MIGRATION_PARAMETER_COMPRESS_THREADS:
                p->has_compress_threads = true;
                visit_type_int(v, param, &p->compress_threads, &err);
                break;
            case MIGRATION_PARAMETER_DECOMPRESS_THREADS:
                p->has_decompress_threads = true;
                visit_type_int(v, param, &p->decompress_threads, &err);
                break;
            case MIGRATION_PARAMETER_CPU_THROTTLE_INITIAL:
                p->has_cpu_throttle_initial = true;
                visit_type_int(v, param, &p->cpu_throttle_initial, &err);
                break;
            case MIGRATION_PARAMETER_CPU_THROTTLE_INCREMENT:
                p->has_cpu_throttle_increment = true;
                visit_type_int(v, param, &p->cpu_throttle_increment, &err);
                break;
            case MIGRATION_PARAMETER_TLS_CREDS:
                p->has_tls_creds = true;
                p->tls_creds = g_new0(StrOrNull, 1);
                p->tls_creds->type = QTYPE_QSTRING;
                visit_type_str(v, param, &p->tls_creds->u.s, &err);
                break;
            case MIGRATION_PARAMETER_TLS_HOSTNAME:
                p->has_tls_hostname = true;
                p->tls_hostname = g_new0(StrOrNull, 1);
                p->tls_hostname->type = QTYPE_QSTRING;
                visit_type_str(v, param, &p->tls_hostname->u.s, &err);
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
            case MIGRATION_PARAMETER_DOWNTIME_LIMIT:
                p->has_downtime_limit = true;
                visit_type_int(v, param, &p->downtime_limit, &err);
                break;
            case MIGRATION_PARAMETER_X_CHECKPOINT_DELAY:
                p->has_x_checkpoint_delay = true;
                visit_type_int(v, param, &p->x_checkpoint_delay, &err);
                break;
            case MIGRATION_PARAMETER_BLOCK_INCREMENTAL:
                p->has_block_incremental = true;
                visit_type_bool(v, param, &p->block_incremental, &err);
                break;
            }

            if (err) {
                goto cleanup;
            }

            qmp_migrate_set_parameters(p, &err);
            break;
        }
    }

    if (i == MIGRATION_PARAMETER__MAX) {
        error_setg(&err, QERR_INVALID_PARAMETER, param);
    }

 cleanup:
    qapi_free_MigrateSetParameters(p);
    visit_free(v);
    if (err) {
        error_report_err(err);
    }
}

void hmp_client_migrate_info(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *protocol = qdict_get_str(qdict, "protocol");
    const char *hostname = qdict_get_str(qdict, "hostname");
    bool has_port        = qdict_haskey(qdict, "port");
    int port             = qdict_get_try_int(qdict, "port", -1);
    bool has_tls_port    = qdict_haskey(qdict, "tls-port");
    int tls_port         = qdict_get_try_int(qdict, "tls-port", -1);
    const char *cert_subject = qdict_get_try_str(qdict, "cert-subject");

    qmp_client_migrate_info(protocol, hostname,
                            has_port, port, has_tls_port, tls_port,
                            !!cert_subject, cert_subject, &err);
    hmp_handle_error(mon, &err);
}

void hmp_migrate_start_postcopy(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    qmp_migrate_start_postcopy(&err);
    hmp_handle_error(mon, &err);
}

void hmp_x_colo_lost_heartbeat(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_x_colo_lost_heartbeat(&err);
    hmp_handle_error(mon, &err);
}

void hmp_set_password(Monitor *mon, const QDict *qdict)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *password  = qdict_get_str(qdict, "password");
    const char *connected = qdict_get_try_str(qdict, "connected");
    Error *err = NULL;

    qmp_set_password(protocol, password, !!connected, connected, &err);
    hmp_handle_error(mon, &err);
}

void hmp_expire_password(Monitor *mon, const QDict *qdict)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *whenstr = qdict_get_str(qdict, "time");
    Error *err = NULL;

    qmp_expire_password(protocol, whenstr, &err);
    hmp_handle_error(mon, &err);
}

void hmp_eject(Monitor *mon, const QDict *qdict)
{
    bool force = qdict_get_try_bool(qdict, "force", false);
    const char *device = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(true, device, false, NULL, true, force, &err);
    hmp_handle_error(mon, &err);
}

static void hmp_change_read_arg(void *opaque, const char *password,
                                void *readline_opaque)
{
    qmp_change_vnc_password(password, NULL);
    monitor_read_command(opaque, 1);
}

void hmp_change(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    const char *read_only = qdict_get_try_str(qdict, "read-only-mode");
    BlockdevChangeReadOnlyMode read_only_mode = 0;
    Error *err = NULL;

    if (strcmp(device, "vnc") == 0) {
        if (read_only) {
            monitor_printf(mon,
                           "Parameter 'read-only-mode' is invalid for VNC\n");
            return;
        }
        if (strcmp(target, "passwd") == 0 ||
            strcmp(target, "password") == 0) {
            if (!arg) {
                monitor_read_password(mon, hmp_change_read_arg, NULL);
                return;
            }
        }
        qmp_change("vnc", target, !!arg, arg, &err);
    } else {
        if (read_only) {
            read_only_mode =
                qapi_enum_parse(BlockdevChangeReadOnlyMode_lookup,
                                read_only, BLOCKDEV_CHANGE_READ_ONLY_MODE__MAX,
                                BLOCKDEV_CHANGE_READ_ONLY_MODE_RETAIN, &err);
            if (err) {
                hmp_handle_error(mon, &err);
                return;
            }
        }

        qmp_blockdev_change_medium(true, device, false, NULL, target,
                                   !!arg, arg, !!read_only, read_only_mode,
                                   &err);
    }

    hmp_handle_error(mon, &err);
}

void hmp_block_set_io_throttle(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    BlockIOThrottle throttle = {
        .has_device = true,
        .device = (char *) qdict_get_str(qdict, "device"),
        .bps = qdict_get_int(qdict, "bps"),
        .bps_rd = qdict_get_int(qdict, "bps_rd"),
        .bps_wr = qdict_get_int(qdict, "bps_wr"),
        .iops = qdict_get_int(qdict, "iops"),
        .iops_rd = qdict_get_int(qdict, "iops_rd"),
        .iops_wr = qdict_get_int(qdict, "iops_wr"),
    };

    qmp_block_set_io_throttle(&throttle, &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_stream(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    const char *base = qdict_get_try_str(qdict, "base");
    int64_t speed = qdict_get_try_int(qdict, "speed", 0);

    qmp_block_stream(true, device, device, base != NULL, base, false, NULL,
                     false, NULL, qdict_haskey(qdict, "speed"), speed,
                     true, BLOCKDEV_ON_ERROR_REPORT, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_set_speed(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    int64_t value = qdict_get_int(qdict, "speed");

    qmp_block_job_set_speed(device, value, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_cancel(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    bool force = qdict_get_try_bool(qdict, "force", false);

    qmp_block_job_cancel(device, true, force, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_pause(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_pause(device, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_resume(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_resume(device, &error);

    hmp_handle_error(mon, &error);
}

void hmp_block_job_complete(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");

    qmp_block_job_complete(device, &error);

    hmp_handle_error(mon, &error);
}

typedef struct HMPMigrationStatus
{
    QEMUTimer *timer;
    Monitor *mon;
    bool is_block_migration;
} HMPMigrationStatus;

static void hmp_migrate_status_cb(void *opaque)
{
    HMPMigrationStatus *status = opaque;
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);
    if (!info->has_status || info->status == MIGRATION_STATUS_ACTIVE ||
        info->status == MIGRATION_STATUS_SETUP) {
        if (info->has_disk) {
            int progress;

            if (info->disk->remaining) {
                progress = info->disk->transferred * 100 / info->disk->total;
            } else {
                progress = 100;
            }

            monitor_printf(status->mon, "Completed %d %%\r", progress);
            monitor_flush(status->mon);
        }

        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
    } else {
        if (status->is_block_migration) {
            monitor_printf(status->mon, "\n");
        }
        if (info->has_error_desc) {
            error_report("%s", info->error_desc);
        }
        monitor_resume(status->mon);
        timer_del(status->timer);
        g_free(status);
    }

    qapi_free_MigrationInfo(info);
}

void hmp_migrate(Monitor *mon, const QDict *qdict)
{
    bool detach = qdict_get_try_bool(qdict, "detach", false);
    bool blk = qdict_get_try_bool(qdict, "blk", false);
    bool inc = qdict_get_try_bool(qdict, "inc", false);
    const char *uri = qdict_get_str(qdict, "uri");
    Error *err = NULL;

    qmp_migrate(uri, !!blk, blk, !!inc, inc, false, false, &err);
    if (err) {
        error_report_err(err);
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
        status->is_block_migration = blk || inc;
        status->timer = timer_new_ms(QEMU_CLOCK_REALTIME, hmp_migrate_status_cb,
                                          status);
        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }
}

void hmp_device_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_device_add((QDict *)qdict, NULL, &err);
    hmp_handle_error(mon, &err);
}

void hmp_device_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    Error *err = NULL;

    qmp_device_del(id, &err);
    hmp_handle_error(mon, &err);
}

void hmp_dump_guest_memory(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    bool paging = qdict_get_try_bool(qdict, "paging", false);
    bool zlib = qdict_get_try_bool(qdict, "zlib", false);
    bool lzo = qdict_get_try_bool(qdict, "lzo", false);
    bool snappy = qdict_get_try_bool(qdict, "snappy", false);
    const char *file = qdict_get_str(qdict, "filename");
    bool has_begin = qdict_haskey(qdict, "begin");
    bool has_length = qdict_haskey(qdict, "length");
    bool has_detach = qdict_haskey(qdict, "detach");
    int64_t begin = 0;
    int64_t length = 0;
    bool detach = false;
    enum DumpGuestMemoryFormat dump_format = DUMP_GUEST_MEMORY_FORMAT_ELF;
    char *prot;

    if (zlib + lzo + snappy > 1) {
        error_setg(&err, "only one of '-z|-l|-s' can be set");
        hmp_handle_error(mon, &err);
        return;
    }

    if (zlib) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB;
    }

    if (lzo) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO;
    }

    if (snappy) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY;
    }

    if (has_begin) {
        begin = qdict_get_int(qdict, "begin");
    }
    if (has_length) {
        length = qdict_get_int(qdict, "length");
    }
    if (has_detach) {
        detach = qdict_get_bool(qdict, "detach");
    }

    prot = g_strconcat("file:", file, NULL);

    qmp_dump_guest_memory(paging, prot, true, detach, has_begin, begin,
                          has_length, length, true, dump_format, &err);
    hmp_handle_error(mon, &err);
    g_free(prot);
}

void hmp_netdev_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_from_qdict(qemu_find_opts("netdev"), qdict, &err);
    if (err) {
        goto out;
    }

    netdev_add(opts, &err);
    if (err) {
        qemu_opts_del(opts);
    }

out:
    hmp_handle_error(mon, &err);
}

void hmp_netdev_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    Error *err = NULL;

    qmp_netdev_del(id, &err);
    hmp_handle_error(mon, &err);
}

void hmp_object_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    QemuOpts *opts;
    Object *obj = NULL;

    opts = qemu_opts_from_qdict(qemu_find_opts("object"), qdict, &err);
    if (err) {
        hmp_handle_error(mon, &err);
        return;
    }

    obj = user_creatable_add_opts(opts, &err);
    qemu_opts_del(opts);

    if (err) {
        hmp_handle_error(mon, &err);
    }
    if (obj) {
        object_unref(obj);
    }
}

void hmp_getfd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_getfd(fdname, &err);
    hmp_handle_error(mon, &err);
}

void hmp_closefd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_closefd(fdname, &err);
    hmp_handle_error(mon, &err);
}

void hmp_sendkey(Monitor *mon, const QDict *qdict)
{
    const char *keys = qdict_get_str(qdict, "keys");
    KeyValueList *keylist, *head = NULL, *tmp = NULL;
    int has_hold_time = qdict_haskey(qdict, "hold-time");
    int hold_time = qdict_get_try_int(qdict, "hold-time", -1);
    Error *err = NULL;
    char *separator;
    int keyname_len;

    while (1) {
        separator = strchr(keys, '-');
        keyname_len = separator ? separator - keys : strlen(keys);

        /* Be compatible with old interface, convert user inputted "<" */
        if (keys[0] == '<' && keyname_len == 1) {
            keys = "less";
            keyname_len = 4;
        }

        keylist = g_malloc0(sizeof(*keylist));
        keylist->value = g_malloc0(sizeof(*keylist->value));

        if (!head) {
            head = keylist;
        }
        if (tmp) {
            tmp->next = keylist;
        }
        tmp = keylist;

        if (strstart(keys, "0x", NULL)) {
            char *endp;
            int value = strtoul(keys, &endp, 0);
            assert(endp <= keys + keyname_len);
            if (endp != keys + keyname_len) {
                goto err_out;
            }
            keylist->value->type = KEY_VALUE_KIND_NUMBER;
            keylist->value->u.number.data = value;
        } else {
            int idx = index_from_key(keys, keyname_len);
            if (idx == Q_KEY_CODE__MAX) {
                goto err_out;
            }
            keylist->value->type = KEY_VALUE_KIND_QCODE;
            keylist->value->u.qcode.data = idx;
        }

        if (!separator) {
            break;
        }
        keys = separator + 1;
    }

    qmp_send_key(head, has_hold_time, hold_time, &err);
    hmp_handle_error(mon, &err);

out:
    qapi_free_KeyValueList(head);
    return;

err_out:
    monitor_printf(mon, "invalid parameter: %.*s\n", keyname_len, keys);
    goto out;
}

void hmp_screendump(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "filename");
    Error *err = NULL;

    qmp_screendump(filename, &err);
    hmp_handle_error(mon, &err);
}

void hmp_nbd_server_start(Monitor *mon, const QDict *qdict)
{
    const char *uri = qdict_get_str(qdict, "uri");
    bool writable = qdict_get_try_bool(qdict, "writable", false);
    bool all = qdict_get_try_bool(qdict, "all", false);
    Error *local_err = NULL;
    BlockInfoList *block_list, *info;
    SocketAddress *addr;

    if (writable && !all) {
        error_setg(&local_err, "-w only valid together with -a");
        goto exit;
    }

    /* First check if the address is valid and start the server.  */
    addr = socket_parse(uri, &local_err);
    if (local_err != NULL) {
        goto exit;
    }

    nbd_server_start(addr, NULL, &local_err);
    qapi_free_SocketAddress(addr);
    if (local_err != NULL) {
        goto exit;
    }

    if (!all) {
        return;
    }

    /* Then try adding all block devices.  If one fails, close all and
     * exit.
     */
    block_list = qmp_query_block(NULL);

    for (info = block_list; info; info = info->next) {
        if (!info->value->has_inserted) {
            continue;
        }

        qmp_nbd_server_add(info->value->device, true, writable, &local_err);

        if (local_err != NULL) {
            qmp_nbd_server_stop(NULL);
            break;
        }
    }

    qapi_free_BlockInfoList(block_list);

exit:
    hmp_handle_error(mon, &local_err);
}

void hmp_nbd_server_add(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    bool writable = qdict_get_try_bool(qdict, "writable", false);
    Error *local_err = NULL;

    qmp_nbd_server_add(device, true, writable, &local_err);

    if (local_err != NULL) {
        hmp_handle_error(mon, &local_err);
    }
}

void hmp_nbd_server_stop(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_nbd_server_stop(&err);
    hmp_handle_error(mon, &err);
}

void hmp_cpu_add(Monitor *mon, const QDict *qdict)
{
    int cpuid;
    Error *err = NULL;

    cpuid = qdict_get_int(qdict, "id");
    qmp_cpu_add(cpuid, &err);
    hmp_handle_error(mon, &err);
}

void hmp_chardev_add(Monitor *mon, const QDict *qdict)
{
    const char *args = qdict_get_str(qdict, "args");
    Error *err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), args, true);
    if (opts == NULL) {
        error_setg(&err, "Parsing chardev args failed");
    } else {
        qemu_chr_new_from_opts(opts, &err);
        qemu_opts_del(opts);
    }
    hmp_handle_error(mon, &err);
}

void hmp_chardev_change(Monitor *mon, const QDict *qdict)
{
    const char *args = qdict_get_str(qdict, "args");
    const char *id;
    Error *err = NULL;
    ChardevBackend *backend = NULL;
    ChardevReturn *ret = NULL;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), args,
                                             true);
    if (!opts) {
        error_setg(&err, "Parsing chardev args failed");
        goto end;
    }

    id = qdict_get_str(qdict, "id");
    if (qemu_opts_id(opts)) {
        error_setg(&err, "Unexpected 'id' parameter");
        goto end;
    }

    backend = qemu_chr_parse_opts(opts, &err);
    if (!backend) {
        goto end;
    }

    ret = qmp_chardev_change(id, backend, &err);

end:
    qapi_free_ChardevReturn(ret);
    qapi_free_ChardevBackend(backend);
    qemu_opts_del(opts);
    hmp_handle_error(mon, &err);
}

void hmp_chardev_remove(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;

    qmp_chardev_remove(qdict_get_str(qdict, "id"), &local_err);
    hmp_handle_error(mon, &local_err);
}

void hmp_chardev_send_break(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;

    qmp_chardev_send_break(qdict_get_str(qdict, "id"), &local_err);
    hmp_handle_error(mon, &local_err);
}

void hmp_qemu_io(Monitor *mon, const QDict *qdict)
{
    BlockBackend *blk;
    BlockBackend *local_blk = NULL;
    AioContext *aio_context;
    const char* device = qdict_get_str(qdict, "device");
    const char* command = qdict_get_str(qdict, "command");
    Error *err = NULL;
    int ret;

    blk = blk_by_name(device);
    if (!blk) {
        BlockDriverState *bs = bdrv_lookup_bs(NULL, device, &err);
        if (bs) {
            blk = local_blk = blk_new(0, BLK_PERM_ALL);
            ret = blk_insert_bs(blk, bs, &err);
            if (ret < 0) {
                goto fail;
            }
        } else {
            goto fail;
        }
    }

    aio_context = blk_get_aio_context(blk);
    aio_context_acquire(aio_context);

    /*
     * Notably absent: Proper permission management. This is sad, but it seems
     * almost impossible to achieve without changing the semantics and thereby
     * limiting the use cases of the qemu-io HMP command.
     *
     * In an ideal world we would unconditionally create a new BlockBackend for
     * qemuio_command(), but we have commands like 'reopen' and want them to
     * take effect on the exact BlockBackend whose name the user passed instead
     * of just on a temporary copy of it.
     *
     * Another problem is that deleting the temporary BlockBackend involves
     * draining all requests on it first, but some qemu-iotests cases want to
     * issue multiple aio_read/write requests and expect them to complete in
     * the background while the monitor has already returned.
     *
     * This is also what prevents us from saving the original permissions and
     * restoring them later: We can't revoke permissions until all requests
     * have completed, and we don't know when that is nor can we really let
     * anything else run before we have revoken them to avoid race conditions.
     *
     * What happens now is that command() in qemu-io-cmds.c can extend the
     * permissions if necessary for the qemu-io command. And they simply stay
     * extended, possibly resulting in a read-only guest device keeping write
     * permissions. Ugly, but it appears to be the lesser evil.
     */
    qemuio_command(blk, command);

    aio_context_release(aio_context);

fail:
    blk_unref(local_blk);
    hmp_handle_error(mon, &err);
}

void hmp_object_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    Error *err = NULL;

    user_creatable_del(id, &err);
    hmp_handle_error(mon, &err);
}

void hmp_info_memdev(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemdevList *memdev_list = qmp_query_memdev(&err);
    MemdevList *m = memdev_list;
    Visitor *v;
    char *str;

    while (m) {
        v = string_output_visitor_new(false, &str);
        visit_type_uint16List(v, NULL, &m->value->host_nodes, NULL);
        monitor_printf(mon, "memory backend: %s\n", m->value->id);
        monitor_printf(mon, "  size:  %" PRId64 "\n", m->value->size);
        monitor_printf(mon, "  merge: %s\n",
                       m->value->merge ? "true" : "false");
        monitor_printf(mon, "  dump: %s\n",
                       m->value->dump ? "true" : "false");
        monitor_printf(mon, "  prealloc: %s\n",
                       m->value->prealloc ? "true" : "false");
        monitor_printf(mon, "  policy: %s\n",
                       HostMemPolicy_lookup[m->value->policy]);
        visit_complete(v, &str);
        monitor_printf(mon, "  host nodes: %s\n", str);

        g_free(str);
        visit_free(v);
        m = m->next;
    }

    monitor_printf(mon, "\n");

    qapi_free_MemdevList(memdev_list);
}

void hmp_info_memory_devices(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemoryDeviceInfoList *info_list = qmp_query_memory_devices(&err);
    MemoryDeviceInfoList *info;
    MemoryDeviceInfo *value;
    PCDIMMDeviceInfo *di;

    for (info = info_list; info; info = info->next) {
        value = info->value;

        if (value) {
            switch (value->type) {
            case MEMORY_DEVICE_INFO_KIND_DIMM:
                di = value->u.dimm.data;

                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_lookup[value->type],
                               di->id ? di->id : "");
                monitor_printf(mon, "  addr: 0x%" PRIx64 "\n", di->addr);
                monitor_printf(mon, "  slot: %" PRId64 "\n", di->slot);
                monitor_printf(mon, "  node: %" PRId64 "\n", di->node);
                monitor_printf(mon, "  size: %" PRIu64 "\n", di->size);
                monitor_printf(mon, "  memdev: %s\n", di->memdev);
                monitor_printf(mon, "  hotplugged: %s\n",
                               di->hotplugged ? "true" : "false");
                monitor_printf(mon, "  hotpluggable: %s\n",
                               di->hotpluggable ? "true" : "false");
                break;
            default:
                break;
            }
        }
    }

    qapi_free_MemoryDeviceInfoList(info_list);
}

void hmp_info_iothreads(Monitor *mon, const QDict *qdict)
{
    IOThreadInfoList *info_list = qmp_query_iothreads(NULL);
    IOThreadInfoList *info;
    IOThreadInfo *value;

    for (info = info_list; info; info = info->next) {
        value = info->value;
        monitor_printf(mon, "%s:\n", value->id);
        monitor_printf(mon, "  thread_id=%" PRId64 "\n", value->thread_id);
        monitor_printf(mon, "  poll-max-ns=%" PRId64 "\n", value->poll_max_ns);
        monitor_printf(mon, "  poll-grow=%" PRId64 "\n", value->poll_grow);
        monitor_printf(mon, "  poll-shrink=%" PRId64 "\n", value->poll_shrink);
    }

    qapi_free_IOThreadInfoList(info_list);
}

void hmp_qom_list(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_try_str(qdict, "path");
    ObjectPropertyInfoList *list;
    Error *err = NULL;

    if (path == NULL) {
        monitor_printf(mon, "/\n");
        return;
    }

    list = qmp_qom_list(path, &err);
    if (err == NULL) {
        ObjectPropertyInfoList *start = list;
        while (list != NULL) {
            ObjectPropertyInfo *value = list->value;

            monitor_printf(mon, "%s (%s)\n",
                           value->name, value->type);
            list = list->next;
        }
        qapi_free_ObjectPropertyInfoList(start);
    }
    hmp_handle_error(mon, &err);
}

void hmp_qom_set(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    const char *value = qdict_get_str(qdict, "value");
    Error *err = NULL;
    bool ambiguous = false;
    Object *obj;

    obj = object_resolve_path(path, &ambiguous);
    if (obj == NULL) {
        error_set(&err, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
    } else {
        if (ambiguous) {
            monitor_printf(mon, "Warning: Path '%s' is ambiguous\n", path);
        }
        object_property_parse(obj, value, property, &err);
    }
    hmp_handle_error(mon, &err);
}

void hmp_rocker(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    RockerSwitch *rocker;
    Error *err = NULL;

    rocker = qmp_query_rocker(name, &err);
    if (err != NULL) {
        hmp_handle_error(mon, &err);
        return;
    }

    monitor_printf(mon, "name: %s\n", rocker->name);
    monitor_printf(mon, "id: 0x%" PRIx64 "\n", rocker->id);
    monitor_printf(mon, "ports: %d\n", rocker->ports);

    qapi_free_RockerSwitch(rocker);
}

void hmp_rocker_ports(Monitor *mon, const QDict *qdict)
{
    RockerPortList *list, *port;
    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    list = qmp_query_rocker_ports(name, &err);
    if (err != NULL) {
        hmp_handle_error(mon, &err);
        return;
    }

    monitor_printf(mon, "            ena/    speed/ auto\n");
    monitor_printf(mon, "      port  link    duplex neg?\n");

    for (port = list; port; port = port->next) {
        monitor_printf(mon, "%10s  %-4s   %-3s  %2s  %-3s\n",
                       port->value->name,
                       port->value->enabled ? port->value->link_up ?
                       "up" : "down" : "!ena",
                       port->value->speed == 10000 ? "10G" : "??",
                       port->value->duplex ? "FD" : "HD",
                       port->value->autoneg ? "Yes" : "No");
    }

    qapi_free_RockerPortList(list);
}

void hmp_rocker_of_dpa_flows(Monitor *mon, const QDict *qdict)
{
    RockerOfDpaFlowList *list, *info;
    const char *name = qdict_get_str(qdict, "name");
    uint32_t tbl_id = qdict_get_try_int(qdict, "tbl_id", -1);
    Error *err = NULL;

    list = qmp_query_rocker_of_dpa_flows(name, tbl_id != -1, tbl_id, &err);
    if (err != NULL) {
        hmp_handle_error(mon, &err);
        return;
    }

    monitor_printf(mon, "prio tbl hits key(mask) --> actions\n");

    for (info = list; info; info = info->next) {
        RockerOfDpaFlow *flow = info->value;
        RockerOfDpaFlowKey *key = flow->key;
        RockerOfDpaFlowMask *mask = flow->mask;
        RockerOfDpaFlowAction *action = flow->action;

        if (flow->hits) {
            monitor_printf(mon, "%-4d %-3d %-4" PRIu64,
                           key->priority, key->tbl_id, flow->hits);
        } else {
            monitor_printf(mon, "%-4d %-3d     ",
                           key->priority, key->tbl_id);
        }

        if (key->has_in_pport) {
            monitor_printf(mon, " pport %d", key->in_pport);
            if (mask->has_in_pport) {
                monitor_printf(mon, "(0x%x)", mask->in_pport);
            }
        }

        if (key->has_vlan_id) {
            monitor_printf(mon, " vlan %d",
                           key->vlan_id & VLAN_VID_MASK);
            if (mask->has_vlan_id) {
                monitor_printf(mon, "(0x%x)", mask->vlan_id);
            }
        }

        if (key->has_tunnel_id) {
            monitor_printf(mon, " tunnel %d", key->tunnel_id);
            if (mask->has_tunnel_id) {
                monitor_printf(mon, "(0x%x)", mask->tunnel_id);
            }
        }

        if (key->has_eth_type) {
            switch (key->eth_type) {
            case 0x0806:
                monitor_printf(mon, " ARP");
                break;
            case 0x0800:
                monitor_printf(mon, " IP");
                break;
            case 0x86dd:
                monitor_printf(mon, " IPv6");
                break;
            case 0x8809:
                monitor_printf(mon, " LACP");
                break;
            case 0x88cc:
                monitor_printf(mon, " LLDP");
                break;
            default:
                monitor_printf(mon, " eth type 0x%04x", key->eth_type);
                break;
            }
        }

        if (key->has_eth_src) {
            if ((strcmp(key->eth_src, "01:00:00:00:00:00") == 0) &&
                (mask->has_eth_src) &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " src <any mcast/bcast>");
            } else if ((strcmp(key->eth_src, "00:00:00:00:00:00") == 0) &&
                (mask->has_eth_src) &&
                (strcmp(mask->eth_src, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " src <any ucast>");
            } else {
                monitor_printf(mon, " src %s", key->eth_src);
                if (mask->has_eth_src) {
                    monitor_printf(mon, "(%s)", mask->eth_src);
                }
            }
        }

        if (key->has_eth_dst) {
            if ((strcmp(key->eth_dst, "01:00:00:00:00:00") == 0) &&
                (mask->has_eth_dst) &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " dst <any mcast/bcast>");
            } else if ((strcmp(key->eth_dst, "00:00:00:00:00:00") == 0) &&
                (mask->has_eth_dst) &&
                (strcmp(mask->eth_dst, "01:00:00:00:00:00") == 0)) {
                monitor_printf(mon, " dst <any ucast>");
            } else {
                monitor_printf(mon, " dst %s", key->eth_dst);
                if (mask->has_eth_dst) {
                    monitor_printf(mon, "(%s)", mask->eth_dst);
                }
            }
        }

        if (key->has_ip_proto) {
            monitor_printf(mon, " proto %d", key->ip_proto);
            if (mask->has_ip_proto) {
                monitor_printf(mon, "(0x%x)", mask->ip_proto);
            }
        }

        if (key->has_ip_tos) {
            monitor_printf(mon, " TOS %d", key->ip_tos);
            if (mask->has_ip_tos) {
                monitor_printf(mon, "(0x%x)", mask->ip_tos);
            }
        }

        if (key->has_ip_dst) {
            monitor_printf(mon, " dst %s", key->ip_dst);
        }

        if (action->has_goto_tbl || action->has_group_id ||
            action->has_new_vlan_id) {
            monitor_printf(mon, " -->");
        }

        if (action->has_new_vlan_id) {
            monitor_printf(mon, " apply new vlan %d",
                           ntohs(action->new_vlan_id));
        }

        if (action->has_group_id) {
            monitor_printf(mon, " write group 0x%08x", action->group_id);
        }

        if (action->has_goto_tbl) {
            monitor_printf(mon, " goto tbl %d", action->goto_tbl);
        }

        monitor_printf(mon, "\n");
    }

    qapi_free_RockerOfDpaFlowList(list);
}

void hmp_rocker_of_dpa_groups(Monitor *mon, const QDict *qdict)
{
    RockerOfDpaGroupList *list, *g;
    const char *name = qdict_get_str(qdict, "name");
    uint8_t type = qdict_get_try_int(qdict, "type", 9);
    Error *err = NULL;
    bool set = false;

    list = qmp_query_rocker_of_dpa_groups(name, type != 9, type, &err);
    if (err != NULL) {
        hmp_handle_error(mon, &err);
        return;
    }

    monitor_printf(mon, "id (decode) --> buckets\n");

    for (g = list; g; g = g->next) {
        RockerOfDpaGroup *group = g->value;

        monitor_printf(mon, "0x%08x", group->id);

        monitor_printf(mon, " (type %s", group->type == 0 ? "L2 interface" :
                                         group->type == 1 ? "L2 rewrite" :
                                         group->type == 2 ? "L3 unicast" :
                                         group->type == 3 ? "L2 multicast" :
                                         group->type == 4 ? "L2 flood" :
                                         group->type == 5 ? "L3 interface" :
                                         group->type == 6 ? "L3 multicast" :
                                         group->type == 7 ? "L3 ECMP" :
                                         group->type == 8 ? "L2 overlay" :
                                         "unknown");

        if (group->has_vlan_id) {
            monitor_printf(mon, " vlan %d", group->vlan_id);
        }

        if (group->has_pport) {
            monitor_printf(mon, " pport %d", group->pport);
        }

        if (group->has_index) {
            monitor_printf(mon, " index %d", group->index);
        }

        monitor_printf(mon, ") -->");

        if (group->has_set_vlan_id && group->set_vlan_id) {
            set = true;
            monitor_printf(mon, " set vlan %d",
                           group->set_vlan_id & VLAN_VID_MASK);
        }

        if (group->has_set_eth_src) {
            if (!set) {
                set = true;
                monitor_printf(mon, " set");
            }
            monitor_printf(mon, " src %s", group->set_eth_src);
        }

        if (group->has_set_eth_dst) {
            if (!set) {
                set = true;
                monitor_printf(mon, " set");
            }
            monitor_printf(mon, " dst %s", group->set_eth_dst);
        }

        set = false;

        if (group->has_ttl_check && group->ttl_check) {
            monitor_printf(mon, " check TTL");
        }

        if (group->has_group_id && group->group_id) {
            monitor_printf(mon, " group id 0x%08x", group->group_id);
        }

        if (group->has_pop_vlan && group->pop_vlan) {
            monitor_printf(mon, " pop vlan");
        }

        if (group->has_out_pport) {
            monitor_printf(mon, " out pport %d", group->out_pport);
        }

        if (group->has_group_ids) {
            struct uint32List *id;

            monitor_printf(mon, " groups [");
            for (id = group->group_ids; id; id = id->next) {
                monitor_printf(mon, "0x%08x", id->value);
                if (id->next) {
                    monitor_printf(mon, ",");
                }
            }
            monitor_printf(mon, "]");
        }

        monitor_printf(mon, "\n");
    }

    qapi_free_RockerOfDpaGroupList(list);
}

void hmp_info_dump(Monitor *mon, const QDict *qdict)
{
    DumpQueryResult *result = qmp_query_dump(NULL);

    assert(result && result->status < DUMP_STATUS__MAX);
    monitor_printf(mon, "Status: %s\n", DumpStatus_lookup[result->status]);

    if (result->status == DUMP_STATUS_ACTIVE) {
        float percent = 0;
        assert(result->total != 0);
        percent = 100.0 * result->completed / result->total;
        monitor_printf(mon, "Finished: %.2f %%\n", percent);
    }

    qapi_free_DumpQueryResult(result);
}

void hmp_info_ramblock(Monitor *mon, const QDict *qdict)
{
    ram_block_dump(mon);
}

void hmp_hotpluggable_cpus(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    HotpluggableCPUList *l = qmp_query_hotpluggable_cpus(&err);
    HotpluggableCPUList *saved = l;
    CpuInstanceProperties *c;

    if (err != NULL) {
        hmp_handle_error(mon, &err);
        return;
    }

    monitor_printf(mon, "Hotpluggable CPUs:\n");
    while (l) {
        monitor_printf(mon, "  type: \"%s\"\n", l->value->type);
        monitor_printf(mon, "  vcpus_count: \"%" PRIu64 "\"\n",
                       l->value->vcpus_count);
        if (l->value->has_qom_path) {
            monitor_printf(mon, "  qom_path: \"%s\"\n", l->value->qom_path);
        }

        c = l->value->props;
        monitor_printf(mon, "  CPUInstance Properties:\n");
        if (c->has_node_id) {
            monitor_printf(mon, "    node-id: \"%" PRIu64 "\"\n", c->node_id);
        }
        if (c->has_socket_id) {
            monitor_printf(mon, "    socket-id: \"%" PRIu64 "\"\n", c->socket_id);
        }
        if (c->has_core_id) {
            monitor_printf(mon, "    core-id: \"%" PRIu64 "\"\n", c->core_id);
        }
        if (c->has_thread_id) {
            monitor_printf(mon, "    thread-id: \"%" PRIu64 "\"\n", c->thread_id);
        }

        l = l->next;
    }

    qapi_free_HotpluggableCPUList(saved);
}

void hmp_info_vm_generation_id(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    GuidInfo *info = qmp_query_vm_generation_id(&err);
    if (info) {
        monitor_printf(mon, "%s\n", info->guid);
    }
    hmp_handle_error(mon, &err);
    qapi_free_GuidInfo(info);
}
