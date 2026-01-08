/*
 * Human Monitor Interface commands
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
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "exec/gdbstub.h"
#include "exec/target_page.h"
#include "gdbstub/enums.h"
#include "monitor/hmp.h"
#include "qemu/help_option.h"
#include "monitor/hmp-target.h"
#include "monitor/monitor-internal.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-misc.h"
#include "qobject/qdict.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "system/hw_accel.h"
#include "system/memory.h"
#include "system/system.h"
#include "disas/disas.h"

bool hmp_handle_error(Monitor *mon, Error *err)
{
    if (err) {
        error_reportf_err(err, "Error: ");
        return true;
    }
    return false;
}

/*
 * Split @str at comma.
 * A null @str defaults to "".
 */
strList *hmp_split_at_comma(const char *str)
{
    char **split = g_strsplit(str ?: "", ",", -1);
    strList *res = NULL;
    strList **tail = &res;
    int i;

    for (i = 0; split[i]; i++) {
        QAPI_LIST_APPEND(tail, split[i]);
    }

    g_free(split);
    return res;
}

void hmp_info_name(Monitor *mon, const QDict *qdict)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->name) {
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

void hmp_quit(Monitor *mon, const QDict *qdict)
{
    monitor_suspend(mon);
    qmp_quit(NULL);
}

void hmp_stop(Monitor *mon, const QDict *qdict)
{
    qmp_stop(NULL);
}

void hmp_sync_profile(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");

    if (op == NULL) {
        bool on = qsp_is_enabled();

        monitor_printf(mon, "sync-profile is %s\n", on ? "on" : "off");
        return;
    }
    if (!strcmp(op, "on")) {
        qsp_enable();
    } else if (!strcmp(op, "off")) {
        qsp_disable();
    } else if (!strcmp(op, "reset")) {
        qsp_reset();
    } else {
        Error *err = NULL;

        error_setg(&err, "invalid parameter '%s',"
                   " expecting 'on', 'off', or 'reset'", op);
        hmp_handle_error(mon, err);
    }
}

void hmp_exit_preconfig(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_x_exit_preconfig(&err);
    hmp_handle_error(mon, err);
}

void hmp_cpu(Monitor *mon, const QDict *qdict)
{
    int64_t cpu_index;

    /* XXX: drop the monitor_set_cpu() usage when all HMP commands that
            use it are converted to the QAPI */
    cpu_index = qdict_get_int(qdict, "index");
    if (monitor_set_cpu(mon, cpu_index) < 0) {
        monitor_printf(mon, "invalid CPU index\n");
    }
}

void hmp_cont(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_cont(&err);
    hmp_handle_error(mon, err);
}

void hmp_change(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    const char *read_only = qdict_get_try_str(qdict, "read-only-mode");
    bool force = qdict_get_try_bool(qdict, "force", false);
    Error *err = NULL;

#ifdef CONFIG_VNC
    if (strcmp(device, "vnc") == 0) {
        hmp_change_vnc(mon, device, target, arg, read_only, force, &err);
    } else
#endif
    {
        hmp_change_medium(mon, device, target, arg, read_only, force, &err);
    }

    hmp_handle_error(mon, err);
}

#ifdef CONFIG_POSIX
void hmp_getfd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_getfd(fdname, &err);
    hmp_handle_error(mon, err);
}
#endif

void hmp_closefd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_closefd(fdname, &err);
    hmp_handle_error(mon, err);
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
        monitor_printf(mon, "  aio-max-batch=%" PRId64 "\n",
                       value->aio_max_batch);
    }

    qapi_free_IOThreadInfoList(info_list);
}

void hmp_help(Monitor *mon, const QDict *qdict)
{
    hmp_help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

void hmp_info_help(Monitor *mon, const QDict *qdict)
{
    hmp_help_cmd(mon, "info");
}

void hmp_info_sync_profile(Monitor *mon, const QDict *qdict)
{
    int64_t max = qdict_get_try_int(qdict, "max", 10);
    bool mean = qdict_get_try_bool(qdict, "mean", false);
    bool coalesce = !qdict_get_try_bool(qdict, "no_coalesce", false);
    enum QSPSortBy sort_by;

    sort_by = mean ? QSP_SORT_BY_AVG_WAIT_TIME : QSP_SORT_BY_TOTAL_WAIT_TIME;
    qsp_report(max, sort_by, coalesce);
}

void hmp_info_history(Monitor *mon, const QDict *qdict)
{
    MonitorHMP *hmp_mon = container_of(mon, MonitorHMP, common);
    int i;
    const char *str;

    if (!hmp_mon->rs) {
        return;
    }
    i = 0;
    for(;;) {
        str = readline_get_history(hmp_mon->rs, i);
        if (!str) {
            break;
        }
        monitor_printf(mon, "%d: '%s'\n", i, str);
        i++;
    }
}

void hmp_logfile(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    if (!qemu_set_log_filename(qdict_get_str(qdict, "filename"), &err)) {
        error_report_err(err);
    }
}

void hmp_log(Monitor *mon, const QDict *qdict)
{
    int mask;
    const char *items = qdict_get_str(qdict, "items");
    Error *err = NULL;

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = qemu_str_to_log_mask(items);
        if (!mask) {
            hmp_help_cmd(mon, "log");
            return;
        }
    }

    if (!qemu_set_log(mask, &err)) {
        error_report_err(err);
    }
}

void hmp_gdbserver(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *device = qdict_get_try_str(qdict, "device");

    if (!device) {
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
    }

    if (!gdbserver_start(device, &err)) {
        error_report_err(err);
    } else if (strcmp(device, "none") == 0) {
        monitor_printf(mon, "Disabled gdbserver\n");
    } else {
        monitor_printf(mon, "Waiting for gdb connection on device '%s'\n",
                       device);
    }
}

void hmp_print(Monitor *mon, const QDict *qdict)
{
    int format = qdict_get_int(qdict, "format");
    hwaddr val = qdict_get_int(qdict, "val");

    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" HWADDR_PRIo, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" HWADDR_PRIx, val);
        break;
    case 'u':
        monitor_printf(mon, "%" HWADDR_PRIu, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" HWADDR_PRId, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
    monitor_printf(mon, "\n");
}

void hmp_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        uint8_t val = address_space_ldub(&address_space_memory, addr,
                                         MEMTXATTRS_UNSPECIFIED, NULL);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += val;
    }
    monitor_printf(mon, "%05d\n", sum);
}

void hmp_ioport_read(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int has_index = qdict_haskey(qdict, "index");
    uint32_t val;
    int suffix;

    if (has_index) {
        int index = qdict_get_int(qdict, "index");
        cpu_outb(addr & IOPORTS_MASK, index & 0xff);
        addr++;
    }
    addr &= 0xffff;

    switch(size) {
    default:
    case 1:
        val = cpu_inb(addr);
        suffix = 'b';
        break;
    case 2:
        val = cpu_inw(addr);
        suffix = 'w';
        break;
    case 4:
        val = cpu_inl(addr);
        suffix = 'l';
        break;
    }
    monitor_printf(mon, "port%c[0x%04x] = 0x%0*x\n",
                   suffix, addr, size * 2, val);
}

void hmp_ioport_write(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int val = qdict_get_int(qdict, "val");

    addr &= IOPORTS_MASK;

    switch (size) {
    default:
    case 1:
        cpu_outb(addr, val);
        break;
    case 2:
        cpu_outw(addr, val);
        break;
    case 4:
        cpu_outl(addr, val);
        break;
    }
}

void hmp_boot_set(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;
    const char *bootdevice = qdict_get_str(qdict, "bootdevice");

    qemu_boot_set(bootdevice, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        monitor_printf(mon, "boot device list now set to %s\n", bootdevice);
    }
}

void hmp_info_mtree(Monitor *mon, const QDict *qdict)
{
    bool flatview = qdict_get_try_bool(qdict, "flatview", false);
    bool dispatch_tree = qdict_get_try_bool(qdict, "dispatch_tree", false);
    bool owner = qdict_get_try_bool(qdict, "owner", false);
    bool disabled = qdict_get_try_bool(qdict, "disabled", false);

    mtree_info(flatview, dispatch_tree, owner, disabled);
}

#if defined(CONFIG_FDT)
void hmp_dumpdtb(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "filename");
    Error *local_err = NULL;

    qmp_dumpdtb(filename, &local_err);

    if (hmp_handle_error(mon, local_err)) {
        return;
    }

    monitor_printf(mon, "DTB dumped to '%s'\n", filename);
}
#endif

/* Set the current CPU defined by the user. Callers must hold BQL. */
int monitor_set_cpu(Monitor *mon, int cpu_index)
{
    CPUState *cpu;

    cpu = qemu_get_cpu(cpu_index);
    if (cpu == NULL) {
        return -1;
    }
    g_free(mon->mon_cpu_path);
    mon->mon_cpu_path = object_get_canonical_path(OBJECT(cpu));
    return 0;
}

/* Callers must hold BQL. */
static CPUState *mon_get_cpu_sync(Monitor *mon, bool synchronize)
{
    CPUState *cpu = NULL;

    if (mon->mon_cpu_path) {
        cpu = (CPUState *) object_resolve_path_type(mon->mon_cpu_path,
                                                    TYPE_CPU, NULL);
        if (!cpu) {
            g_free(mon->mon_cpu_path);
            mon->mon_cpu_path = NULL;
        }
    }
    if (!mon->mon_cpu_path) {
        if (!first_cpu) {
            return NULL;
        }
        monitor_set_cpu(mon, first_cpu->cpu_index);
        cpu = first_cpu;
    }
    assert(cpu != NULL);
    if (synchronize) {
        cpu_synchronize_state(cpu);
    }
    return cpu;
}

CPUState *mon_get_cpu(Monitor *mon)
{
    return mon_get_cpu_sync(mon, true);
}

CPUArchState *mon_get_cpu_env(Monitor *mon)
{
    CPUState *cs = mon_get_cpu(mon);

    return cs ? cpu_env(cs) : NULL;
}

int monitor_get_cpu_index(Monitor *mon)
{
    CPUState *cs = mon_get_cpu_sync(mon, false);

    return cs ? cs->cpu_index : UNASSIGNED_CPU_INDEX;
}

void hmp_info_registers(Monitor *mon, const QDict *qdict)
{
    bool all_cpus = qdict_get_try_bool(qdict, "cpustate_all", false);
    int vcpu = qdict_get_try_int(qdict, "vcpu", -1);
    CPUState *cs;

    if (all_cpus) {
        CPU_FOREACH(cs) {
            monitor_printf(mon, "\nCPU#%d\n", cs->cpu_index);
            cpu_dump_state(cs, NULL, CPU_DUMP_FPU | CPU_DUMP_VPU);
        }
    } else {
        cs = vcpu >= 0 ? qemu_get_cpu(vcpu) : mon_get_cpu(mon);

        if (!cs) {
            if (vcpu >= 0) {
                monitor_printf(mon, "CPU#%d not available\n", vcpu);
            } else {
                monitor_printf(mon, "No CPU available\n");
            }
            return;
        }

        monitor_printf(mon, "\nCPU#%d\n", cs->cpu_index);
        cpu_dump_state(cs, NULL, CPU_DUMP_FPU | CPU_DUMP_VPU);
    }
}

static void memory_dump(Monitor *mon, int count, int format, int wsize,
                        uint64_t addr, bool is_physical)
{
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;
    CPUState *cs = mon_get_cpu(mon);
    const unsigned int addr_width = is_physical ? 8 : (target_long_bits() * 2);
    const bool big_endian = target_big_endian();

    if (!cs && (format == 'i' || !is_physical)) {
        monitor_printf(mon, "Can not dump without CPU\n");
        return;
    }

    if (format == 'i') {
        monitor_disas(mon, cs, addr, count, is_physical);
        return;
    }

    len = wsize * count;
    if (wsize == 1) {
        line_size = 8;
    } else {
        line_size = 16;
    }
    max_digits = 0;

    switch (format) {
    case 'o':
        max_digits = DIV_ROUND_UP(wsize * 8, 3);
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = DIV_ROUND_UP(wsize * 8 * 10, 33);
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        monitor_printf(mon, "%0*" PRIx64 ":", addr_width, addr);
        l = len;
        if (l > line_size) {
            l = line_size;
        }
        if (is_physical) {
            AddressSpace *as = cs ? cs->as : &address_space_memory;
            MemTxResult r = address_space_read(as, addr,
                                               MEMTXATTRS_UNSPECIFIED, buf, l);
            if (r != MEMTX_OK) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        } else {
            if (cpu_memory_rw_debug(cs, addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch (wsize) {
            default:
            case 1:
                v = ldub_p(buf + i);
                break;
            case 2:
                v = (big_endian ? lduw_be_p : lduw_le_p)(buf + i);
                break;
            case 4:
                v = (uint32_t)(big_endian ? ldl_be_p : ldl_le_p)(buf + i);
                break;
            case 8:
                v = (big_endian ? ldq_be_p : ldq_le_p)(buf + i);
                break;
            }
            monitor_printf(mon, " ");
            switch (format) {
            case 'o':
                monitor_printf(mon, "0%*" PRIo64, max_digits, v);
                break;
            case 'x':
                monitor_printf(mon, "0x%0*" PRIx64, max_digits, v);
                break;
            case 'u':
                monitor_printf(mon, "%*" PRIu64, max_digits, v);
                break;
            case 'd':
                monitor_printf(mon, "%*" PRId64, max_digits, v);
                break;
            case 'c':
                monitor_printc(mon, v);
                break;
            }
            i += wsize;
        }
        monitor_printf(mon, "\n");
        addr += l;
        len -= l;
    }
}

void hmp_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    vaddr addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, false);
}

void hmp_physical_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    hwaddr addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, true);
}

void *gpa2hva(MemoryRegion **p_mr, hwaddr addr, uint64_t size, Error **errp)
{
    Int128 gpa_region_size;
    MemoryRegionSection mrs = memory_region_find(get_system_memory(),
                                                 addr, size);

    if (!mrs.mr) {
        error_setg(errp,
                   "No memory is mapped at address 0x%" HWADDR_PRIx, addr);
        return NULL;
    }

    if (!memory_region_is_ram(mrs.mr) && !memory_region_is_romd(mrs.mr)) {
        error_setg(errp,
                   "Memory at address 0x%" HWADDR_PRIx " is not RAM", addr);
        memory_region_unref(mrs.mr);
        return NULL;
    }

    gpa_region_size = int128_make64(size);
    if (int128_lt(mrs.size, gpa_region_size)) {
        error_setg(errp, "Size of memory region at 0x%" HWADDR_PRIx
                   " exceeded.", addr);
        memory_region_unref(mrs.mr);
        return NULL;
    }

    *p_mr = mrs.mr;
    return qemu_map_ram_ptr(mrs.mr->ram_block, mrs.offset_within_region);
}

void hmp_gpa2hva(Monitor *mon, const QDict *qdict)
{
    hwaddr addr = qdict_get_int(qdict, "addr");
    Error *local_err = NULL;
    MemoryRegion *mr = NULL;
    void *ptr;

    ptr = gpa2hva(&mr, addr, 1, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    monitor_printf(mon, "Host virtual address for 0x%" HWADDR_PRIx
                   " (%s) is %p\n",
                   addr, mr->name, ptr);

    memory_region_unref(mr);
}

void hmp_gva2gpa(Monitor *mon, const QDict *qdict)
{
    vaddr addr = qdict_get_int(qdict, "addr");
    CPUState *cs = mon_get_cpu(mon);
    hwaddr gpa;

    if (!cs) {
        monitor_printf(mon, "No cpu\n");
        return;
    }

    gpa  = cpu_get_phys_page_debug(cs, addr & TARGET_PAGE_MASK);
    if (gpa == -1) {
        monitor_printf(mon, "Unmapped\n");
    } else {
        monitor_printf(mon, "gpa: 0x%" HWADDR_PRIx "\n",
                       gpa + (addr & ~TARGET_PAGE_MASK));
    }
}

#ifdef CONFIG_LINUX
static uint64_t vtop(void *ptr, Error **errp)
{
    uint64_t pinfo;
    uint64_t ret = -1;
    uintptr_t addr = (uintptr_t) ptr;
    uintptr_t pagesize = qemu_real_host_page_size();
    off_t offset = addr / pagesize * sizeof(pinfo);
    int fd;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1) {
        error_setg_file_open(errp, errno, "/proc/self/pagemap");
        return -1;
    }

    /* Force copy-on-write if necessary.  */
    qatomic_add((uint8_t *)ptr, 0);

    if (pread(fd, &pinfo, sizeof(pinfo), offset) != sizeof(pinfo)) {
        error_setg_errno(errp, errno, "Cannot read pagemap");
        goto out;
    }
    if ((pinfo & (1ull << 63)) == 0) {
        error_setg(errp, "Page not present");
        goto out;
    }
    ret = (pinfo & 0x007fffffffffffffull) * pagesize;
    ret |= addr & (pagesize - 1);

out:
    close(fd);
    return ret;
}

void hmp_gpa2hpa(Monitor *mon, const QDict *qdict)
{
    hwaddr addr = qdict_get_int(qdict, "addr");
    Error *local_err = NULL;
    MemoryRegion *mr = NULL;
    void *ptr;
    uint64_t physaddr;

    ptr = gpa2hva(&mr, addr, 1, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    physaddr = vtop(ptr, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        monitor_printf(mon, "Host physical address for 0x%" HWADDR_PRIx
                       " (%s) is 0x%" PRIx64 "\n",
                       addr, mr->name, (uint64_t) physaddr);
    }

    memory_region_unref(mr);
}
#endif
