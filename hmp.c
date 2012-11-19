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

#include "hmp.h"
#include "net.h"
#include "qemu-option.h"
#include "qemu-timer.h"
#include "qmp-commands.h"
#include "qemu_socket.h"
#include "monitor.h"
#include "console.h"

static void hmp_handle_error(Monitor *mon, Error **errp)
{
    if (error_is_set(errp)) {
        monitor_printf(mon, "%s\n", error_get_pretty(*errp));
        error_free(*errp);
    }
}

void hmp_info_name(Monitor *mon)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->has_name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qapi_free_NameInfo(info);
}

void hmp_info_version(Monitor *mon)
{
    VersionInfo *info;

    info = qmp_query_version(NULL);

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                   info->qemu.major, info->qemu.minor, info->qemu.micro,
                   info->package);

    qapi_free_VersionInfo(info);
}

void hmp_info_kvm(Monitor *mon)
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

void hmp_info_status(Monitor *mon)
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

void hmp_info_uuid(Monitor *mon)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qapi_free_UuidInfo(info);
}

void hmp_info_chardev(Monitor *mon)
{
    ChardevInfoList *char_info, *info;

    char_info = qmp_query_chardev(NULL);
    for (info = char_info; info; info = info->next) {
        monitor_printf(mon, "%s: filename=%s\n", info->value->label,
                                                 info->value->filename);
    }

    qapi_free_ChardevInfoList(char_info);
}

void hmp_info_mice(Monitor *mon)
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

void hmp_info_migrate(Monitor *mon)
{
    MigrationInfo *info;
    MigrationCapabilityStatusList *caps, *cap;

    info = qmp_query_migrate(NULL);
    caps = qmp_query_migrate_capabilities(NULL);

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
        monitor_printf(mon, "Migration status: %s\n", info->status);
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
    }

    if (info->has_ram) {
        monitor_printf(mon, "transferred ram: %" PRIu64 " kbytes\n",
                       info->ram->transferred >> 10);
        monitor_printf(mon, "remaining ram: %" PRIu64 " kbytes\n",
                       info->ram->remaining >> 10);
        monitor_printf(mon, "total ram: %" PRIu64 " kbytes\n",
                       info->ram->total >> 10);
        monitor_printf(mon, "duplicate: %" PRIu64 " pages\n",
                       info->ram->duplicate);
        monitor_printf(mon, "normal: %" PRIu64 " pages\n",
                       info->ram->normal);
        monitor_printf(mon, "normal bytes: %" PRIu64 " kbytes\n",
                       info->ram->normal_bytes >> 10);
        if (info->ram->dirty_pages_rate) {
            monitor_printf(mon, "dirty pages rate: %" PRIu64 " pages\n",
                           info->ram->dirty_pages_rate);
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
        monitor_printf(mon, "xbzrle overflow : %" PRIu64 "\n",
                       info->xbzrle_cache->overflow);
    }

    qapi_free_MigrationInfo(info);
    qapi_free_MigrationCapabilityStatusList(caps);
}

void hmp_info_migrate_capabilities(Monitor *mon)
{
    MigrationCapabilityStatusList *caps, *cap;

    caps = qmp_query_migrate_capabilities(NULL);

    if (caps) {
        monitor_printf(mon, "capabilities: ");
        for (cap = caps; cap; cap = cap->next) {
            monitor_printf(mon, "%s: %s ",
                           MigrationCapability_lookup[cap->value->capability],
                           cap->value->state ? "on" : "off");
        }
        monitor_printf(mon, "\n");
    }

    qapi_free_MigrationCapabilityStatusList(caps);
}

void hmp_info_migrate_cache_size(Monitor *mon)
{
    monitor_printf(mon, "xbzrel cache size: %" PRId64 " kbytes\n",
                   qmp_query_migrate_cache_size(NULL) >> 10);
}

void hmp_info_cpus(Monitor *mon)
{
    CpuInfoList *cpu_list, *cpu;

    cpu_list = qmp_query_cpus(NULL);

    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        int active = ' ';

        if (cpu->value->CPU == monitor_get_cpu_index()) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ":", active, cpu->value->CPU);

        if (cpu->value->has_pc) {
            monitor_printf(mon, " pc=0x%016" PRIx64, cpu->value->pc);
        }
        if (cpu->value->has_nip) {
            monitor_printf(mon, " nip=0x%016" PRIx64, cpu->value->nip);
        }
        if (cpu->value->has_npc) {
            monitor_printf(mon, " npc=0x%016" PRIx64, cpu->value->npc);
        }
        if (cpu->value->has_PC) {
            monitor_printf(mon, " PC=0x%016" PRIx64, cpu->value->PC);
        }

        if (cpu->value->halted) {
            monitor_printf(mon, " (halted)");
        }

        monitor_printf(mon, " thread_id=%" PRId64 "\n", cpu->value->thread_id);
    }

    qapi_free_CpuInfoList(cpu_list);
}

void hmp_info_block(Monitor *mon)
{
    BlockInfoList *block_list, *info;

    block_list = qmp_query_block(NULL);

    for (info = block_list; info; info = info->next) {
        monitor_printf(mon, "%s: removable=%d",
                       info->value->device, info->value->removable);

        if (info->value->removable) {
            monitor_printf(mon, " locked=%d", info->value->locked);
            monitor_printf(mon, " tray-open=%d", info->value->tray_open);
        }

        if (info->value->has_io_status) {
            monitor_printf(mon, " io-status=%s",
                           BlockDeviceIoStatus_lookup[info->value->io_status]);
        }

        if (info->value->has_inserted) {
            monitor_printf(mon, " file=");
            monitor_print_filename(mon, info->value->inserted->file);

            if (info->value->inserted->has_backing_file) {
                monitor_printf(mon, " backing_file=");
                monitor_print_filename(mon, info->value->inserted->backing_file);
                monitor_printf(mon, " backing_file_depth=%" PRId64,
                    info->value->inserted->backing_file_depth);
            }
            monitor_printf(mon, " ro=%d drv=%s encrypted=%d",
                           info->value->inserted->ro,
                           info->value->inserted->drv,
                           info->value->inserted->encrypted);

            monitor_printf(mon, " bps=%" PRId64 " bps_rd=%" PRId64
                            " bps_wr=%" PRId64 " iops=%" PRId64
                            " iops_rd=%" PRId64 " iops_wr=%" PRId64,
                            info->value->inserted->bps,
                            info->value->inserted->bps_rd,
                            info->value->inserted->bps_wr,
                            info->value->inserted->iops,
                            info->value->inserted->iops_rd,
                            info->value->inserted->iops_wr);
        } else {
            monitor_printf(mon, " [not inserted]");
        }

        monitor_printf(mon, "\n");
    }

    qapi_free_BlockInfoList(block_list);
}

void hmp_info_blockstats(Monitor *mon)
{
    BlockStatsList *stats_list, *stats;

    stats_list = qmp_query_blockstats(NULL);

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
                       "\n",
                       stats->value->stats->rd_bytes,
                       stats->value->stats->wr_bytes,
                       stats->value->stats->rd_operations,
                       stats->value->stats->wr_operations,
                       stats->value->stats->flush_operations,
                       stats->value->stats->wr_total_time_ns,
                       stats->value->stats->rd_total_time_ns,
                       stats->value->stats->flush_total_time_ns);
    }

    qapi_free_BlockStatsList(stats_list);
}

void hmp_info_vnc(Monitor *mon)
{
    VncInfo *info;
    Error *err = NULL;
    VncClientInfoList *client;

    info = qmp_query_vnc(&err);
    if (err) {
        monitor_printf(mon, "%s\n", error_get_pretty(err));
        error_free(err);
        return;
    }

    if (!info->enabled) {
        monitor_printf(mon, "Server: disabled\n");
        goto out;
    }

    monitor_printf(mon, "Server:\n");
    if (info->has_host && info->has_service) {
        monitor_printf(mon, "     address: %s:%s\n", info->host, info->service);
    }
    if (info->has_auth) {
        monitor_printf(mon, "        auth: %s\n", info->auth);
    }

    if (!info->has_clients || info->clients == NULL) {
        monitor_printf(mon, "Client: none\n");
    } else {
        for (client = info->clients; client; client = client->next) {
            monitor_printf(mon, "Client:\n");
            monitor_printf(mon, "     address: %s:%s\n",
                           client->value->host, client->value->service);
            monitor_printf(mon, "  x509_dname: %s\n",
                           client->value->x509_dname ?
                           client->value->x509_dname : "none");
            monitor_printf(mon, "    username: %s\n",
                           client->value->has_sasl_username ?
                           client->value->sasl_username : "none");
        }
    }

out:
    qapi_free_VncInfo(info);
}

void hmp_info_spice(Monitor *mon)
{
    SpiceChannelList *chan;
    SpiceInfo *info;

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
        }
    }

out:
    qapi_free_SpiceInfo(info);
}

void hmp_info_balloon(Monitor *mon)
{
    BalloonInfo *info;
    Error *err = NULL;

    info = qmp_query_balloon(&err);
    if (err) {
        monitor_printf(mon, "%s\n", error_get_pretty(err));
        error_free(err);
        return;
    }

    monitor_printf(mon, "balloon: actual=%" PRId64, info->actual >> 20);
    if (info->has_mem_swapped_in) {
        monitor_printf(mon, " mem_swapped_in=%" PRId64, info->mem_swapped_in);
    }
    if (info->has_mem_swapped_out) {
        monitor_printf(mon, " mem_swapped_out=%" PRId64, info->mem_swapped_out);
    }
    if (info->has_major_page_faults) {
        monitor_printf(mon, " major_page_faults=%" PRId64,
                       info->major_page_faults);
    }
    if (info->has_minor_page_faults) {
        monitor_printf(mon, " minor_page_faults=%" PRId64,
                       info->minor_page_faults);
    }
    if (info->has_free_mem) {
        monitor_printf(mon, " free_mem=%" PRId64, info->free_mem);
    }
    if (info->has_total_mem) {
        monitor_printf(mon, " total_mem=%" PRId64, info->total_mem);
    }

    monitor_printf(mon, "\n");

    qapi_free_BalloonInfo(info);
}

static void hmp_info_pci_device(Monitor *mon, const PciDeviceInfo *dev)
{
    PciMemoryRegionList *region;

    monitor_printf(mon, "  Bus %2" PRId64 ", ", dev->bus);
    monitor_printf(mon, "device %3" PRId64 ", function %" PRId64 ":\n",
                   dev->slot, dev->function);
    monitor_printf(mon, "    ");

    if (dev->class_info.has_desc) {
        monitor_printf(mon, "%s", dev->class_info.desc);
    } else {
        monitor_printf(mon, "Class %04" PRId64, dev->class_info.class);
    }

    monitor_printf(mon, ": PCI device %04" PRIx64 ":%04" PRIx64 "\n",
                   dev->id.vendor, dev->id.device);

    if (dev->has_irq) {
        monitor_printf(mon, "      IRQ %" PRId64 ".\n", dev->irq);
    }

    if (dev->has_pci_bridge) {
        monitor_printf(mon, "      BUS %" PRId64 ".\n",
                       dev->pci_bridge->bus.number);
        monitor_printf(mon, "      secondary bus %" PRId64 ".\n",
                       dev->pci_bridge->bus.secondary);
        monitor_printf(mon, "      subordinate bus %" PRId64 ".\n",
                       dev->pci_bridge->bus.subordinate);

        monitor_printf(mon, "      IO range [0x%04"PRIx64", 0x%04"PRIx64"]\n",
                       dev->pci_bridge->bus.io_range->base,
                       dev->pci_bridge->bus.io_range->limit);

        monitor_printf(mon,
                       "      memory range [0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus.memory_range->base,
                       dev->pci_bridge->bus.memory_range->limit);

        monitor_printf(mon, "      prefetchable memory range "
                       "[0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus.prefetchable_range->base,
                       dev->pci_bridge->bus.prefetchable_range->limit);
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

void hmp_info_pci(Monitor *mon)
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

void hmp_info_block_jobs(Monitor *mon)
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
    Error *errp = NULL;

    qmp_memsave(addr, size, filename, true, monitor_get_cpu_index(), &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_pmemsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *errp = NULL;

    qmp_pmemsave(addr, size, filename, &errp);
    hmp_handle_error(mon, &errp);
}

static void hmp_cont_cb(void *opaque, int err)
{
    if (!err) {
        qmp_cont(NULL);
    }
}

static bool key_is_missing(const BlockInfo *bdev)
{
    return (bdev->inserted && bdev->inserted->encryption_key_missing);
}

void hmp_cont(Monitor *mon, const QDict *qdict)
{
    BlockInfoList *bdev_list, *bdev;
    Error *errp = NULL;

    bdev_list = qmp_query_block(NULL);
    for (bdev = bdev_list; bdev; bdev = bdev->next) {
        if (key_is_missing(bdev->value)) {
            monitor_read_block_device_key(mon, bdev->value->device,
                                          hmp_cont_cb, NULL);
            goto out;
        }
    }

    qmp_cont(&errp);
    hmp_handle_error(mon, &errp);

out:
    qapi_free_BlockInfoList(bdev_list);
}

void hmp_system_wakeup(Monitor *mon, const QDict *qdict)
{
    qmp_system_wakeup(NULL);
}

void hmp_inject_nmi(Monitor *mon, const QDict *qdict)
{
    Error *errp = NULL;

    qmp_inject_nmi(&errp);
    hmp_handle_error(mon, &errp);
}

void hmp_set_link(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    int up = qdict_get_bool(qdict, "up");
    Error *errp = NULL;

    qmp_set_link(name, up, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *errp = NULL;

    qmp_block_passwd(device, password, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_balloon(Monitor *mon, const QDict *qdict)
{
    int64_t value = qdict_get_int(qdict, "value");
    Error *errp = NULL;

    qmp_balloon(value, &errp);
    if (error_is_set(&errp)) {
        monitor_printf(mon, "balloon: %s\n", error_get_pretty(errp));
        error_free(errp);
    }
}

void hmp_block_resize(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    int64_t size = qdict_get_int(qdict, "size");
    Error *errp = NULL;

    qmp_block_resize(device, size, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_drive_mirror(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_str(qdict, "target");
    const char *format = qdict_get_try_str(qdict, "format");
    int reuse = qdict_get_try_bool(qdict, "reuse", 0);
    int full = qdict_get_try_bool(qdict, "full", 0);
    enum NewImageMode mode;
    Error *errp = NULL;

    if (!filename) {
        error_set(&errp, QERR_MISSING_PARAMETER, "target");
        hmp_handle_error(mon, &errp);
        return;
    }

    if (reuse) {
        mode = NEW_IMAGE_MODE_EXISTING;
    } else {
        mode = NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    }

    qmp_drive_mirror(device, filename, !!format, format,
                     full ? MIRROR_SYNC_MODE_FULL : MIRROR_SYNC_MODE_TOP,
                     true, mode, false, 0,
                     false, 0, false, 0, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_snapshot_blkdev(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *filename = qdict_get_try_str(qdict, "snapshot-file");
    const char *format = qdict_get_try_str(qdict, "format");
    int reuse = qdict_get_try_bool(qdict, "reuse", 0);
    enum NewImageMode mode;
    Error *errp = NULL;

    if (!filename) {
        /* In the future, if 'snapshot-file' is not specified, the snapshot
           will be taken internally. Today it's actually required. */
        error_set(&errp, QERR_MISSING_PARAMETER, "snapshot-file");
        hmp_handle_error(mon, &errp);
        return;
    }

    mode = reuse ? NEW_IMAGE_MODE_EXISTING : NEW_IMAGE_MODE_ABSOLUTE_PATHS;
    qmp_blockdev_snapshot_sync(device, filename, !!format, format,
                               true, mode, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_migrate_cancel(Monitor *mon, const QDict *qdict)
{
    qmp_migrate_cancel(NULL);
}

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
        monitor_printf(mon, "%s\n", error_get_pretty(err));
        error_free(err);
        return;
    }
}

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

    for (i = 0; i < MIGRATION_CAPABILITY_MAX; i++) {
        if (strcmp(cap, MigrationCapability_lookup[i]) == 0) {
            caps->value = g_malloc0(sizeof(*caps->value));
            caps->value->capability = i;
            caps->value->state = state;
            caps->next = NULL;
            qmp_migrate_set_capabilities(caps, &err);
            break;
        }
    }

    if (i == MIGRATION_CAPABILITY_MAX) {
        error_set(&err, QERR_INVALID_PARAMETER, cap);
    }

    qapi_free_MigrationCapabilityStatusList(caps);

    if (err) {
        monitor_printf(mon, "migrate_set_parameter: %s\n",
                       error_get_pretty(err));
        error_free(err);
    }
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
    int force = qdict_get_try_bool(qdict, "force", 0);
    const char *device = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(device, true, force, &err);
    hmp_handle_error(mon, &err);
}

static void hmp_change_read_arg(Monitor *mon, const char *password,
                                void *opaque)
{
    qmp_change_vnc_password(password, NULL);
    monitor_read_command(mon, 1);
}

void hmp_change(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    Error *err = NULL;

    if (strcmp(device, "vnc") == 0 &&
            (strcmp(target, "passwd") == 0 ||
             strcmp(target, "password") == 0)) {
        if (!arg) {
            monitor_read_password(mon, hmp_change_read_arg, NULL);
            return;
        }
    }

    qmp_change(device, target, !!arg, arg, &err);
    if (error_is_set(&err) &&
        error_get_class(err) == ERROR_CLASS_DEVICE_ENCRYPTED) {
        error_free(err);
        monitor_read_block_device_key(mon, device, NULL, NULL);
        return;
    }
    hmp_handle_error(mon, &err);
}

void hmp_block_set_io_throttle(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_block_set_io_throttle(qdict_get_str(qdict, "device"),
                              qdict_get_int(qdict, "bps"),
                              qdict_get_int(qdict, "bps_rd"),
                              qdict_get_int(qdict, "bps_wr"),
                              qdict_get_int(qdict, "iops"),
                              qdict_get_int(qdict, "iops_rd"),
                              qdict_get_int(qdict, "iops_wr"), &err);
    hmp_handle_error(mon, &err);
}

void hmp_block_stream(Monitor *mon, const QDict *qdict)
{
    Error *error = NULL;
    const char *device = qdict_get_str(qdict, "device");
    const char *base = qdict_get_try_str(qdict, "base");
    int64_t speed = qdict_get_try_int(qdict, "speed", 0);

    qmp_block_stream(device, base != NULL, base,
                     qdict_haskey(qdict, "speed"), speed,
                     BLOCKDEV_ON_ERROR_REPORT, true, &error);

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
    bool force = qdict_get_try_bool(qdict, "force", 0);

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

typedef struct MigrationStatus
{
    QEMUTimer *timer;
    Monitor *mon;
    bool is_block_migration;
} MigrationStatus;

static void hmp_migrate_status_cb(void *opaque)
{
    MigrationStatus *status = opaque;
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);
    if (!info->has_status || strcmp(info->status, "active") == 0) {
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

        qemu_mod_timer(status->timer, qemu_get_clock_ms(rt_clock) + 1000);
    } else {
        if (status->is_block_migration) {
            monitor_printf(status->mon, "\n");
        }
        monitor_resume(status->mon);
        qemu_del_timer(status->timer);
        g_free(status);
    }

    qapi_free_MigrationInfo(info);
}

void hmp_migrate(Monitor *mon, const QDict *qdict)
{
    int detach = qdict_get_try_bool(qdict, "detach", 0);
    int blk = qdict_get_try_bool(qdict, "blk", 0);
    int inc = qdict_get_try_bool(qdict, "inc", 0);
    const char *uri = qdict_get_str(qdict, "uri");
    Error *err = NULL;

    qmp_migrate(uri, !!blk, blk, !!inc, inc, false, false, &err);
    if (err) {
        monitor_printf(mon, "migrate: %s\n", error_get_pretty(err));
        error_free(err);
        return;
    }

    if (!detach) {
        MigrationStatus *status;

        if (monitor_suspend(mon) < 0) {
            monitor_printf(mon, "terminal does not allow synchronous "
                           "migration, continuing detached\n");
            return;
        }

        status = g_malloc0(sizeof(*status));
        status->mon = mon;
        status->is_block_migration = blk || inc;
        status->timer = qemu_new_timer_ms(rt_clock, hmp_migrate_status_cb,
                                          status);
        qemu_mod_timer(status->timer, qemu_get_clock_ms(rt_clock));
    }
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
    Error *errp = NULL;
    int paging = qdict_get_try_bool(qdict, "paging", 0);
    const char *file = qdict_get_str(qdict, "filename");
    bool has_begin = qdict_haskey(qdict, "begin");
    bool has_length = qdict_haskey(qdict, "length");
    int64_t begin = 0;
    int64_t length = 0;
    char *prot;

    if (has_begin) {
        begin = qdict_get_int(qdict, "begin");
    }
    if (has_length) {
        length = qdict_get_int(qdict, "length");
    }

    prot = g_strconcat("file:", file, NULL);

    qmp_dump_guest_memory(paging, prot, has_begin, begin, has_length, length,
                          &errp);
    hmp_handle_error(mon, &errp);
    g_free(prot);
}

void hmp_netdev_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_from_qdict(qemu_find_opts("netdev"), qdict, &err);
    if (error_is_set(&err)) {
        goto out;
    }

    netdev_add(opts, &err);
    if (error_is_set(&err)) {
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

void hmp_getfd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *errp = NULL;

    qmp_getfd(fdname, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_closefd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *errp = NULL;

    qmp_closefd(fdname, &errp);
    hmp_handle_error(mon, &errp);
}

void hmp_send_key(Monitor *mon, const QDict *qdict)
{
    const char *keys = qdict_get_str(qdict, "keys");
    KeyValueList *keylist, *head = NULL, *tmp = NULL;
    int has_hold_time = qdict_haskey(qdict, "hold-time");
    int hold_time = qdict_get_try_int(qdict, "hold-time", -1);
    Error *err = NULL;
    char keyname_buf[16];
    char *separator;
    int keyname_len;

    while (1) {
        separator = strchr(keys, '-');
        keyname_len = separator ? separator - keys : strlen(keys);
        pstrcpy(keyname_buf, sizeof(keyname_buf), keys);

        /* Be compatible with old interface, convert user inputted "<" */
        if (!strncmp(keyname_buf, "<", 1) && keyname_len == 1) {
            pstrcpy(keyname_buf, sizeof(keyname_buf), "less");
            keyname_len = 4;
        }
        keyname_buf[keyname_len] = 0;

        keylist = g_malloc0(sizeof(*keylist));
        keylist->value = g_malloc0(sizeof(*keylist->value));

        if (!head) {
            head = keylist;
        }
        if (tmp) {
            tmp->next = keylist;
        }
        tmp = keylist;

        if (strstart(keyname_buf, "0x", NULL)) {
            char *endp;
            int value = strtoul(keyname_buf, &endp, 0);
            if (*endp != '\0') {
                goto err_out;
            }
            keylist->value->kind = KEY_VALUE_KIND_NUMBER;
            keylist->value->number = value;
        } else {
            int idx = index_from_key(keyname_buf);
            if (idx == Q_KEY_CODE_MAX) {
                goto err_out;
            }
            keylist->value->kind = KEY_VALUE_KIND_QCODE;
            keylist->value->qcode = idx;
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
    monitor_printf(mon, "invalid parameter: %s\n", keyname_buf);
    goto out;
}

void hmp_screen_dump(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "filename");
    Error *err = NULL;

    qmp_screendump(filename, &err);
    hmp_handle_error(mon, &err);
}

void hmp_nbd_server_start(Monitor *mon, const QDict *qdict)
{
    const char *uri = qdict_get_str(qdict, "uri");
    int writable = qdict_get_try_bool(qdict, "writable", 0);
    int all = qdict_get_try_bool(qdict, "all", 0);
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

    qmp_nbd_server_start(addr, &local_err);
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
    int writable = qdict_get_try_bool(qdict, "writable", 0);
    Error *local_err = NULL;

    qmp_nbd_server_add(device, true, writable, &local_err);

    if (local_err != NULL) {
        hmp_handle_error(mon, &local_err);
    }
}

void hmp_nbd_server_stop(Monitor *mon, const QDict *qdict)
{
    Error *errp = NULL;

    qmp_nbd_server_stop(&errp);
    hmp_handle_error(mon, &errp);
}
