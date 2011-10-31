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
 */

#include "hmp.h"
#include "qmp-commands.h"

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

    info = qmp_query_migrate(NULL);

    if (info->has_status) {
        monitor_printf(mon, "Migration status: %s\n", info->status);
    }

    if (info->has_ram) {
        monitor_printf(mon, "transferred ram: %" PRIu64 " kbytes\n",
                       info->ram->transferred >> 10);
        monitor_printf(mon, "remaining ram: %" PRIu64 " kbytes\n",
                       info->ram->remaining >> 10);
        monitor_printf(mon, "total ram: %" PRIu64 " kbytes\n",
                       info->ram->total >> 10);
    }

    if (info->has_disk) {
        monitor_printf(mon, "transferred disk: %" PRIu64 " kbytes\n",
                       info->disk->transferred >> 10);
        monitor_printf(mon, "remaining disk: %" PRIu64 " kbytes\n",
                       info->disk->remaining >> 10);
        monitor_printf(mon, "total disk: %" PRIu64 " kbytes\n",
                       info->disk->total >> 10);
    }

    qapi_free_MigrationInfo(info);
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

        monitor_printf(mon, "%c CPU #%" PRId64 ": ", active, cpu->value->CPU);

        if (cpu->value->has_pc) {
            monitor_printf(mon, "pc=0x%016" PRIx64, cpu->value->pc);
        }
        if (cpu->value->has_nip) {
            monitor_printf(mon, "nip=0x%016" PRIx64, cpu->value->nip);
        }
        if (cpu->value->has_npc) {
            monitor_printf(mon, "pc=0x%016" PRIx64, cpu->value->pc);
            monitor_printf(mon, "npc=0x%016" PRIx64, cpu->value->npc);
        }
        if (cpu->value->has_PC) {
            monitor_printf(mon, "PC=0x%016" PRIx64, cpu->value->PC);
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
            }
            monitor_printf(mon, " ro=%d drv=%s encrypted=%d",
                           info->value->inserted->ro,
                           info->value->inserted->drv,
                           info->value->inserted->encrypted);
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
    monitor_printf(mon, "        auth: %s\n", info->auth);
    monitor_printf(mon, "    compiled: %s\n", info->compiled_version);

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
    PciInfoList *info;
    Error *err = NULL;

    info = qmp_query_pci(&err);
    if (err) {
        monitor_printf(mon, "PCI devices not supported\n");
        error_free(err);
        return;
    }

    for (; info; info = info->next) {
        PciDeviceInfoList *dev;

        for (dev = info->value->devices; dev; dev = dev->next) {
            hmp_info_pci_device(mon, dev->value);
        }
    }

    qapi_free_PciInfoList(info);
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
