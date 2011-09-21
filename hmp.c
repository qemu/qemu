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
