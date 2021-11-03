/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "monitor-internal.h"
#include "monitor/qdev.h"
#include "hw/usb.h"
#include "hw/pci/pci.h"
#include "sysemu/watchdog.h"
#include "hw/loader.h"
#include "exec/gdbstub.h"
#include "net/net.h"
#include "net/slirp.h"
#include "ui/qemu-spice.h"
#include "qemu/config-file.h"
#include "qemu/ctype.h"
#include "ui/console.h"
#include "ui/input.h"
#include "audio/audio.h"
#include "disas/disas.h"
#include "sysemu/balloon.h"
#include "qemu/timer.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "authz/list.h"
#include "qapi/util.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/tpm.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "trace/control.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#include "exec/memory.h"
#include "exec/exec-all.h"
#include "qemu/option.h"
#include "qemu/thread.h"
#include "block/qapi.h"
#include "block/block-hmp-cmds.h"
#include "qapi/qapi-commands-char.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-commands-trace.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-init-commands.h"
#include "qapi/error.h"
#include "qapi/qmp-event.h"
#include "sysemu/cpus.h"
#include "qemu/cutils.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#endif

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

/* file descriptor associated with a file descriptor set */
typedef struct MonFdsetFd MonFdsetFd;
struct MonFdsetFd {
    int fd;
    bool removed;
    char *opaque;
    QLIST_ENTRY(MonFdsetFd) next;
};

/* file descriptor set containing fds passed via SCM_RIGHTS */
typedef struct MonFdset MonFdset;
struct MonFdset {
    int64_t id;
    QLIST_HEAD(, MonFdsetFd) fds;
    QLIST_HEAD(, MonFdsetFd) dup_fds;
    QLIST_ENTRY(MonFdset) next;
};

/* Protects mon_fdsets */
static QemuMutex mon_fdsets_lock;
static QLIST_HEAD(, MonFdset) mon_fdsets;

static HMPCommand hmp_info_cmds[];

char *qmp_human_monitor_command(const char *command_line, bool has_cpu_index,
                                int64_t cpu_index, Error **errp)
{
    char *output = NULL;
    MonitorHMP hmp = {};

    monitor_data_init(&hmp.common, false, true, false);

    if (has_cpu_index) {
        int ret = monitor_set_cpu(&hmp.common, cpu_index);
        if (ret < 0) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                       "a CPU number");
            goto out;
        }
    }

    handle_hmp_command(&hmp, command_line);

    WITH_QEMU_LOCK_GUARD(&hmp.common.mon_lock) {
        output = g_strdup(hmp.common.outbuf->str);
    }

out:
    monitor_data_destroy(&hmp.common);
    return output;
}

/**
 * Is @name in the '|' separated list of names @list?
 */
int hmp_compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for (;;) {
        pstart = p;
        p = qemu_strchrnul(p, '|');
        if ((p - pstart) == len && !memcmp(pstart, name, len)) {
            return 1;
        }
        if (*p == '\0') {
            break;
        }
        p++;
    }
    return 0;
}

static void do_help_cmd(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

static void hmp_trace_event(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    Error *local_err = NULL;

    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    qmp_trace_event_set_state(tp_name, new_state, true, true, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

#ifdef CONFIG_TRACE_SIMPLE
static void hmp_trace_file(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");
    const char *arg = qdict_get_try_str(qdict, "arg");

    if (!op) {
        st_print_trace_file_status();
    } else if (!strcmp(op, "on")) {
        st_set_trace_file_enabled(true);
    } else if (!strcmp(op, "off")) {
        st_set_trace_file_enabled(false);
    } else if (!strcmp(op, "flush")) {
        st_flush_trace_buffer();
    } else if (!strcmp(op, "set")) {
        if (arg) {
            st_set_trace_file(arg);
        }
    } else {
        monitor_printf(mon, "unexpected argument \"%s\"\n", op);
        help_cmd(mon, "trace-file");
    }
}
#endif

static void hmp_info_help(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, "info");
}

static void monitor_init_qmp_commands(void)
{
    /*
     * Two command lists:
     * - qmp_commands contains all QMP commands
     * - qmp_cap_negotiation_commands contains just
     *   "qmp_capabilities", to enforce capability negotiation
     */

    qmp_init_marshal(&qmp_commands);

    qmp_register_command(&qmp_commands, "device_add",
                         qmp_device_add, 0, 0);

    QTAILQ_INIT(&qmp_cap_negotiation_commands);
    qmp_register_command(&qmp_cap_negotiation_commands, "qmp_capabilities",
                         qmp_marshal_qmp_capabilities,
                         QCO_ALLOW_PRECONFIG, 0);
}

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

    return cs ? cs->env_ptr : NULL;
}

int monitor_get_cpu_index(Monitor *mon)
{
    CPUState *cs = mon_get_cpu_sync(mon, false);

    return cs ? cs->cpu_index : UNASSIGNED_CPU_INDEX;
}

static void hmp_info_registers(Monitor *mon, const QDict *qdict)
{
    bool all_cpus = qdict_get_try_bool(qdict, "cpustate_all", false);
    CPUState *cs;

    if (all_cpus) {
        CPU_FOREACH(cs) {
            monitor_printf(mon, "\nCPU#%d\n", cs->cpu_index);
            cpu_dump_state(cs, NULL, CPU_DUMP_FPU);
        }
    } else {
        cs = mon_get_cpu(mon);

        if (!cs) {
            monitor_printf(mon, "No CPU available\n");
            return;
        }

        cpu_dump_state(cs, NULL, CPU_DUMP_FPU);
    }
}

static void hmp_info_sync_profile(Monitor *mon, const QDict *qdict)
{
    int64_t max = qdict_get_try_int(qdict, "max", 10);
    bool mean = qdict_get_try_bool(qdict, "mean", false);
    bool coalesce = !qdict_get_try_bool(qdict, "no_coalesce", false);
    enum QSPSortBy sort_by;

    sort_by = mean ? QSP_SORT_BY_AVG_WAIT_TIME : QSP_SORT_BY_TOTAL_WAIT_TIME;
    qsp_report(max, sort_by, coalesce);
}

static void hmp_info_history(Monitor *mon, const QDict *qdict)
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

static void hmp_info_trace_events(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_try_str(qdict, "name");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    TraceEventInfoList *events;
    TraceEventInfoList *elem;
    Error *local_err = NULL;

    if (name == NULL) {
        name = "*";
    }
    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    events = qmp_trace_event_get_state(name, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    for (elem = events; elem != NULL; elem = elem->next) {
        monitor_printf(mon, "%s : state %u\n",
                       elem->value->name,
                       elem->value->state == TRACE_EVENT_STATE_ENABLED ? 1 : 0);
    }
    qapi_free_TraceEventInfoList(events);
}

void qmp_client_migrate_info(const char *protocol, const char *hostname,
                             bool has_port, int64_t port,
                             bool has_tls_port, int64_t tls_port,
                             bool has_cert_subject, const char *cert_subject,
                             Error **errp)
{
    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }

        if (!has_port && !has_tls_port) {
            error_setg(errp, QERR_MISSING_PARAMETER, "port/tls-port");
            return;
        }

        if (qemu_spice.migrate_info(hostname,
                                    has_port ? port : -1,
                                    has_tls_port ? tls_port : -1,
                                    cert_subject)) {
            error_setg(errp, "Could not set up display for migration");
            return;
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol", "'spice'");
}

static void hmp_logfile(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qemu_set_log_filename(qdict_get_str(qdict, "filename"), &err);
    if (err) {
        error_report_err(err);
    }
}

static void hmp_log(Monitor *mon, const QDict *qdict)
{
    int mask;
    const char *items = qdict_get_str(qdict, "items");

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = qemu_str_to_log_mask(items);
        if (!mask) {
            help_cmd(mon, "log");
            return;
        }
    }
    qemu_set_log(mask);
}

static void hmp_singlestep(Monitor *mon, const QDict *qdict)
{
    const char *option = qdict_get_try_str(qdict, "option");
    if (!option || !strcmp(option, "on")) {
        singlestep = 1;
    } else if (!strcmp(option, "off")) {
        singlestep = 0;
    } else {
        monitor_printf(mon, "unexpected option %s\n", option);
    }
}

static void hmp_gdbserver(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_try_str(qdict, "device");
    if (!device) {
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
    }

    if (gdbserver_start(device) < 0) {
        monitor_printf(mon, "Could not open gdbserver on device '%s'\n",
                       device);
    } else if (strcmp(device, "none") == 0) {
        monitor_printf(mon, "Disabled gdbserver\n");
    } else {
        monitor_printf(mon, "Waiting for gdb connection on device '%s'\n",
                       device);
    }
}

static void hmp_watchdog_action(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    WatchdogAction action;
    char *qapi_value;

    qapi_value = g_ascii_strdown(qdict_get_str(qdict, "action"), -1);
    action = qapi_enum_parse(&WatchdogAction_lookup, qapi_value, -1, &err);
    g_free(qapi_value);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }
    qmp_watchdog_set_action(action, &error_abort);
}

static void monitor_printc(Monitor *mon, int c)
{
    monitor_printf(mon, "'");
    switch(c) {
    case '\'':
        monitor_printf(mon, "\\'");
        break;
    case '\\':
        monitor_printf(mon, "\\\\");
        break;
    case '\n':
        monitor_printf(mon, "\\n");
        break;
    case '\r':
        monitor_printf(mon, "\\r");
        break;
    default:
        if (c >= 32 && c <= 126) {
            monitor_printf(mon, "%c", c);
        } else {
            monitor_printf(mon, "\\x%02x", c);
        }
        break;
    }
    monitor_printf(mon, "'");
}

static void memory_dump(Monitor *mon, int count, int format, int wsize,
                        hwaddr addr, int is_physical)
{
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;
    CPUState *cs = mon_get_cpu(mon);

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

    switch(format) {
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
        if (is_physical) {
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        } else {
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        }
        l = len;
        if (l > line_size)
            l = line_size;
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
            switch(wsize) {
            default:
            case 1:
                v = ldub_p(buf + i);
                break;
            case 2:
                v = lduw_p(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_p(buf + i);
                break;
            case 8:
                v = ldq_p(buf + i);
                break;
            }
            monitor_printf(mon, " ");
            switch(format) {
            case 'o':
                monitor_printf(mon, "%#*" PRIo64, max_digits, v);
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

static void hmp_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_long addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 0);
}

static void hmp_physical_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    hwaddr addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 1);
}

void *gpa2hva(MemoryRegion **p_mr, hwaddr addr, uint64_t size, Error **errp)
{
    Int128 gpa_region_size;
    MemoryRegionSection mrs = memory_region_find(get_system_memory(),
                                                 addr, size);

    if (!mrs.mr) {
        error_setg(errp, "No memory is mapped at address 0x%" HWADDR_PRIx, addr);
        return NULL;
    }

    if (!memory_region_is_ram(mrs.mr) && !memory_region_is_romd(mrs.mr)) {
        error_setg(errp, "Memory at address 0x%" HWADDR_PRIx "is not RAM", addr);
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

static void hmp_gpa2hva(Monitor *mon, const QDict *qdict)
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

static void hmp_gva2gpa(Monitor *mon, const QDict *qdict)
{
    target_ulong addr = qdict_get_int(qdict, "addr");
    MemTxAttrs attrs;
    CPUState *cs = mon_get_cpu(mon);
    hwaddr gpa;

    if (!cs) {
        monitor_printf(mon, "No cpu\n");
        return;
    }

    gpa  = cpu_get_phys_page_attrs_debug(cs, addr & TARGET_PAGE_MASK, &attrs);
    if (gpa == -1) {
        monitor_printf(mon, "Unmapped\n");
    } else {
        monitor_printf(mon, "gpa: %#" HWADDR_PRIx "\n",
                       gpa + (addr & ~TARGET_PAGE_MASK));
    }
}

#ifdef CONFIG_LINUX
static uint64_t vtop(void *ptr, Error **errp)
{
    uint64_t pinfo;
    uint64_t ret = -1;
    uintptr_t addr = (uintptr_t) ptr;
    uintptr_t pagesize = qemu_real_host_page_size;
    off_t offset = addr / pagesize * sizeof(pinfo);
    int fd;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "Cannot open /proc/self/pagemap");
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
    ret = ((pinfo & 0x007fffffffffffffull) * pagesize) | (addr & (pagesize - 1));

out:
    close(fd);
    return ret;
}

static void hmp_gpa2hpa(Monitor *mon, const QDict *qdict)
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

static void do_print(Monitor *mon, const QDict *qdict)
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

static void hmp_sum(Monitor *mon, const QDict *qdict)
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

static int mouse_button_state;

static void hmp_mouse_move(Monitor *mon, const QDict *qdict)
{
    int dx, dy, dz, button;
    const char *dx_str = qdict_get_str(qdict, "dx_str");
    const char *dy_str = qdict_get_str(qdict, "dy_str");
    const char *dz_str = qdict_get_try_str(qdict, "dz_str");

    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    qemu_input_queue_rel(NULL, INPUT_AXIS_X, dx);
    qemu_input_queue_rel(NULL, INPUT_AXIS_Y, dy);

    if (dz_str) {
        dz = strtol(dz_str, NULL, 0);
        if (dz != 0) {
            button = (dz > 0) ? INPUT_BUTTON_WHEEL_UP : INPUT_BUTTON_WHEEL_DOWN;
            qemu_input_queue_btn(NULL, button, true);
            qemu_input_event_sync();
            qemu_input_queue_btn(NULL, button, false);
        }
    }
    qemu_input_event_sync();
}

static void hmp_mouse_button(Monitor *mon, const QDict *qdict)
{
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]       = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE]     = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]      = MOUSE_EVENT_RBUTTON,
    };
    int button_state = qdict_get_int(qdict, "button_state");

    if (mouse_button_state == button_state) {
        return;
    }
    qemu_input_update_buttons(NULL, bmap, mouse_button_state, button_state);
    qemu_input_event_sync();
    mouse_button_state = button_state;
}

static void hmp_ioport_read(Monitor *mon, const QDict *qdict)
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

static void hmp_ioport_write(Monitor *mon, const QDict *qdict)
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

static void hmp_boot_set(Monitor *mon, const QDict *qdict)
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

static void hmp_info_mtree(Monitor *mon, const QDict *qdict)
{
    bool flatview = qdict_get_try_bool(qdict, "flatview", false);
    bool dispatch_tree = qdict_get_try_bool(qdict, "dispatch_tree", false);
    bool owner = qdict_get_try_bool(qdict, "owner", false);
    bool disabled = qdict_get_try_bool(qdict, "disabled", false);

    mtree_info(flatview, dispatch_tree, owner, disabled);
}

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static void hmp_info_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

static void hmp_stopcapture(Monitor *mon, const QDict *qdict)
{
    int i;
    int n = qdict_get_int(qdict, "n");
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            QLIST_REMOVE (s, entries);
            g_free (s);
            return;
        }
    }
}

static void hmp_wavcapture(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    int freq = qdict_get_try_int(qdict, "freq", 44100);
    int bits = qdict_get_try_int(qdict, "bits", 16);
    int nchannels = qdict_get_try_int(qdict, "nchannels", 2);
    const char *audiodev = qdict_get_str(qdict, "audiodev");
    CaptureState *s;
    AudioState *as = audio_state_by_name(audiodev);

    if (!as) {
        monitor_printf(mon, "Audiodev '%s' not found\n", audiodev);
        return;
    }

    s = g_malloc0 (sizeof (*s));

    if (wav_start_capture(as, s, path, freq, bits, nchannels)) {
        monitor_printf(mon, "Failed to add wave capture\n");
        g_free (s);
        return;
    }
    QLIST_INSERT_HEAD (&capture_head, s, entries);
}

void qmp_getfd(const char *fdname, Error **errp)
{
    Monitor *cur_mon = monitor_cur();
    mon_fd_t *monfd;
    int fd, tmp_fd;

    fd = qemu_chr_fe_get_msgfd(&cur_mon->chr);
    if (fd == -1) {
        error_setg(errp, "No file descriptor supplied via SCM_RIGHTS");
        return;
    }

    if (qemu_isdigit(fdname[0])) {
        close(fd);
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdname",
                   "a name not starting with a digit");
        return;
    }

    QEMU_LOCK_GUARD(&cur_mon->mon_lock);
    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        tmp_fd = monfd->fd;
        monfd->fd = fd;
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return;
    }

    monfd = g_malloc0(sizeof(mon_fd_t));
    monfd->name = g_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&cur_mon->fds, monfd, next);
}

void qmp_closefd(const char *fdname, Error **errp)
{
    Monitor *cur_mon = monitor_cur();
    mon_fd_t *monfd;
    int tmp_fd;

    qemu_mutex_lock(&cur_mon->mon_lock);
    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        tmp_fd = monfd->fd;
        g_free(monfd->name);
        g_free(monfd);
        qemu_mutex_unlock(&cur_mon->mon_lock);
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return;
    }

    qemu_mutex_unlock(&cur_mon->mon_lock);
    error_setg(errp, "File descriptor named '%s' not found", fdname);
}

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    QEMU_LOCK_GUARD(&mon->mon_lock);
    QLIST_FOREACH(monfd, &mon->fds, next) {
        int fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        fd = monfd->fd;

        /* caller takes ownership of fd */
        QLIST_REMOVE(monfd, next);
        g_free(monfd->name);
        g_free(monfd);

        return fd;
    }

    error_setg(errp, "File descriptor named '%s' has not been found", fdname);
    return -1;
}

static void monitor_fdset_cleanup(MonFdset *mon_fdset)
{
    MonFdsetFd *mon_fdset_fd;
    MonFdsetFd *mon_fdset_fd_next;

    QLIST_FOREACH_SAFE(mon_fdset_fd, &mon_fdset->fds, next, mon_fdset_fd_next) {
        if ((mon_fdset_fd->removed ||
                (QLIST_EMPTY(&mon_fdset->dup_fds) && mon_refcount == 0)) &&
                runstate_is_running()) {
            close(mon_fdset_fd->fd);
            g_free(mon_fdset_fd->opaque);
            QLIST_REMOVE(mon_fdset_fd, next);
            g_free(mon_fdset_fd);
        }
    }

    if (QLIST_EMPTY(&mon_fdset->fds) && QLIST_EMPTY(&mon_fdset->dup_fds)) {
        QLIST_REMOVE(mon_fdset, next);
        g_free(mon_fdset);
    }
}

void monitor_fdsets_cleanup(void)
{
    MonFdset *mon_fdset;
    MonFdset *mon_fdset_next;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH_SAFE(mon_fdset, &mon_fdsets, next, mon_fdset_next) {
        monitor_fdset_cleanup(mon_fdset);
    }
}

AddfdInfo *qmp_add_fd(bool has_fdset_id, int64_t fdset_id, bool has_opaque,
                      const char *opaque, Error **errp)
{
    int fd;
    Monitor *mon = monitor_cur();
    AddfdInfo *fdinfo;

    fd = qemu_chr_fe_get_msgfd(&mon->chr);
    if (fd == -1) {
        error_setg(errp, "No file descriptor supplied via SCM_RIGHTS");
        goto error;
    }

    fdinfo = monitor_fdset_add_fd(fd, has_fdset_id, fdset_id,
                                  has_opaque, opaque, errp);
    if (fdinfo) {
        return fdinfo;
    }

error:
    if (fd != -1) {
        close(fd);
    }
    return NULL;
}

void qmp_remove_fd(int64_t fdset_id, bool has_fd, int64_t fd, Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    char fd_str[60];

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            if (has_fd) {
                if (mon_fdset_fd->fd != fd) {
                    continue;
                }
                mon_fdset_fd->removed = true;
                break;
            } else {
                mon_fdset_fd->removed = true;
            }
        }
        if (has_fd && !mon_fdset_fd) {
            goto error;
        }
        monitor_fdset_cleanup(mon_fdset);
        return;
    }

error:
    if (has_fd) {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64 ", fd:%" PRId64,
                 fdset_id, fd);
    } else {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64, fdset_id);
    }
    error_setg(errp, "File descriptor named '%s' not found", fd_str);
}

FdsetInfoList *qmp_query_fdsets(Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    FdsetInfoList *fdset_list = NULL;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        FdsetInfo *fdset_info = g_malloc0(sizeof(*fdset_info));

        fdset_info->fdset_id = mon_fdset->id;

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            FdsetFdInfo *fdsetfd_info;

            fdsetfd_info = g_malloc0(sizeof(*fdsetfd_info));
            fdsetfd_info->fd = mon_fdset_fd->fd;
            if (mon_fdset_fd->opaque) {
                fdsetfd_info->has_opaque = true;
                fdsetfd_info->opaque = g_strdup(mon_fdset_fd->opaque);
            } else {
                fdsetfd_info->has_opaque = false;
            }

            QAPI_LIST_PREPEND(fdset_info->fds, fdsetfd_info);
        }

        QAPI_LIST_PREPEND(fdset_list, fdset_info);
    }

    return fdset_list;
}

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                bool has_opaque, const char *opaque,
                                Error **errp)
{
    MonFdset *mon_fdset = NULL;
    MonFdsetFd *mon_fdset_fd;
    AddfdInfo *fdinfo;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    if (has_fdset_id) {
        QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
            /* Break if match found or match impossible due to ordering by ID */
            if (fdset_id <= mon_fdset->id) {
                if (fdset_id < mon_fdset->id) {
                    mon_fdset = NULL;
                }
                break;
            }
        }
    }

    if (mon_fdset == NULL) {
        int64_t fdset_id_prev = -1;
        MonFdset *mon_fdset_cur = QLIST_FIRST(&mon_fdsets);

        if (has_fdset_id) {
            if (fdset_id < 0) {
                error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdset-id",
                           "a non-negative value");
                return NULL;
            }
            /* Use specified fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id < mon_fdset_cur->id) {
                    break;
                }
            }
        } else {
            /* Use first available fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id_prev == mon_fdset_cur->id - 1) {
                    fdset_id_prev = mon_fdset_cur->id;
                    continue;
                }
                break;
            }
        }

        mon_fdset = g_malloc0(sizeof(*mon_fdset));
        if (has_fdset_id) {
            mon_fdset->id = fdset_id;
        } else {
            mon_fdset->id = fdset_id_prev + 1;
        }

        /* The fdset list is ordered by fdset ID */
        if (!mon_fdset_cur) {
            QLIST_INSERT_HEAD(&mon_fdsets, mon_fdset, next);
        } else if (mon_fdset->id < mon_fdset_cur->id) {
            QLIST_INSERT_BEFORE(mon_fdset_cur, mon_fdset, next);
        } else {
            QLIST_INSERT_AFTER(mon_fdset_cur, mon_fdset, next);
        }
    }

    mon_fdset_fd = g_malloc0(sizeof(*mon_fdset_fd));
    mon_fdset_fd->fd = fd;
    mon_fdset_fd->removed = false;
    if (has_opaque) {
        mon_fdset_fd->opaque = g_strdup(opaque);
    }
    QLIST_INSERT_HEAD(&mon_fdset->fds, mon_fdset_fd, next);

    fdinfo = g_malloc0(sizeof(*fdinfo));
    fdinfo->fdset_id = mon_fdset->id;
    fdinfo->fd = mon_fdset_fd->fd;

    return fdinfo;
}

int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags)
{
#ifdef _WIN32
    return -ENOENT;
#else
    MonFdset *mon_fdset;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        MonFdsetFd *mon_fdset_fd;
        MonFdsetFd *mon_fdset_fd_dup;
        int fd = -1;
        int dup_fd;
        int mon_fd_flags;

        if (mon_fdset->id != fdset_id) {
            continue;
        }

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            mon_fd_flags = fcntl(mon_fdset_fd->fd, F_GETFL);
            if (mon_fd_flags == -1) {
                return -1;
            }

            if ((flags & O_ACCMODE) == (mon_fd_flags & O_ACCMODE)) {
                fd = mon_fdset_fd->fd;
                break;
            }
        }

        if (fd == -1) {
            errno = EACCES;
            return -1;
        }

        dup_fd = qemu_dup_flags(fd, flags);
        if (dup_fd == -1) {
            return -1;
        }

        mon_fdset_fd_dup = g_malloc0(sizeof(*mon_fdset_fd_dup));
        mon_fdset_fd_dup->fd = dup_fd;
        QLIST_INSERT_HEAD(&mon_fdset->dup_fds, mon_fdset_fd_dup, next);
        return dup_fd;
    }

    errno = ENOENT;
    return -1;
#endif
}

static int64_t monitor_fdset_dup_fd_find_remove(int dup_fd, bool remove)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                if (remove) {
                    QLIST_REMOVE(mon_fdset_fd_dup, next);
                    g_free(mon_fdset_fd_dup);
                    if (QLIST_EMPTY(&mon_fdset->dup_fds)) {
                        monitor_fdset_cleanup(mon_fdset);
                    }
                    return -1;
                } else {
                    return mon_fdset->id;
                }
            }
        }
    }

    return -1;
}

int64_t monitor_fdset_dup_fd_find(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, false);
}

void monitor_fdset_dup_fd_remove(int dup_fd)
{
    monitor_fdset_dup_fd_find_remove(dup_fd, true);
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    int fd;
    Error *local_err = NULL;

    if (!qemu_isdigit(fdname[0]) && mon) {
        fd = monitor_get_fd(mon, fdname, &local_err);
    } else {
        fd = qemu_parse_fd(fdname);
        if (fd == -1) {
            error_setg(&local_err, "Invalid file descriptor number '%s'",
                       fdname);
        }
    }
    if (local_err) {
        error_propagate(errp, local_err);
        assert(fd == -1);
    } else {
        assert(fd != -1);
    }

    return fd;
}

/* Please update hmp-commands.hx when adding or changing commands */
static HMPCommand hmp_info_cmds[] = {
#include "hmp-commands-info.h"
    { NULL, NULL, },
};

/* hmp_cmds and hmp_info_cmds would be sorted at runtime */
HMPCommand hmp_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

/*
 * Set @pval to the value in the register identified by @name.
 * return 0 if OK, -1 if not found
 */
int get_monitor_def(Monitor *mon, int64_t *pval, const char *name)
{
    const MonitorDef *md = target_monitor_defs();
    CPUState *cs = mon_get_cpu(mon);
    void *ptr;
    uint64_t tmp = 0;
    int ret;

    if (cs == NULL || md == NULL) {
        return -1;
    }

    for(; md->name != NULL; md++) {
        if (hmp_compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(mon, md, md->offset);
            } else {
                CPUArchState *env = mon_get_cpu_env(mon);
                ptr = (uint8_t *)env + md->offset;
                switch(md->type) {
                case MD_I32:
                    *pval = *(int32_t *)ptr;
                    break;
                case MD_TLONG:
                    *pval = *(target_long *)ptr;
                    break;
                default:
                    *pval = 0;
                    break;
                }
            }
            return 0;
        }
    }

    ret = target_get_monitor_def(cs, name, &tmp);
    if (!ret) {
        *pval = (target_long) tmp;
    }

    return ret;
}

static void add_completion_option(ReadLineState *rs, const char *str,
                                  const char *option)
{
    if (!str || !option) {
        return;
    }
    if (!strncmp(option, str, strlen(str))) {
        readline_add_completion(rs, option);
    }
}

void chardev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevBackendInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev_backends(NULL);
    while (list) {
        const char *chr_name = list->value->name;

        if (!strncmp(chr_name, str, len)) {
            readline_add_completion(rs, chr_name);
        }
        list = list->next;
    }
    qapi_free_ChardevBackendInfoList(start);
}

void netdev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    int i;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < NET_CLIENT_DRIVER__MAX; i++) {
        add_completion_option(rs, str, NetClientDriver_str(i));
    }
}

void device_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_DEVICE, false);
    while (elt) {
        const char *name;
        DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                             TYPE_DEVICE);
        name = object_class_get_name(OBJECT_CLASS(dc));

        if (dc->user_creatable
            && !strncmp(name, str, len)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

void object_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_USER_CREATABLE, false);
    while (elt) {
        const char *name;

        name = object_class_get_name(OBJECT_CLASS(elt->data));
        if (!strncmp(name, str, len) && strcmp(name, TYPE_USER_CREATABLE)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

static int qdev_add_hotpluggable_device(Object *obj, void *opaque)
{
    GSList **list = opaque;
    DeviceState *dev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);

    if (dev == NULL) {
        return 0;
    }

    if (dev->realized && object_property_get_bool(obj, "hotpluggable", NULL)) {
        *list = g_slist_append(*list, dev);
    }

    return 0;
}

static GSList *qdev_build_hotpluggable_device_list(Object *peripheral)
{
    GSList *list = NULL;

    object_child_foreach(peripheral, qdev_add_hotpluggable_device, &list);

    return list;
}

static void peripheral_device_del_completion(ReadLineState *rs,
                                             const char *str, size_t len)
{
    Object *peripheral = container_get(qdev_get_machine(), "/peripheral");
    GSList *list, *item;

    list = qdev_build_hotpluggable_device_list(peripheral);
    if (!list) {
        return;
    }

    for (item = list; item; item = g_slist_next(item)) {
        DeviceState *dev = item->data;

        if (dev->id && !strncmp(str, dev->id, len)) {
            readline_add_completion(rs, dev->id);
        }
    }

    g_slist_free(list);
}

void chardev_remove_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr = list->value;

        if (!strncmp(chr->label, str, len)) {
            readline_add_completion(rs, chr->label);
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

static void ringbuf_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr_info = list->value;

        if (!strncmp(chr_info->label, str, len)) {
            Chardev *chr = qemu_chr_find(chr_info->label);
            if (chr && CHARDEV_IS_RINGBUF(chr)) {
                readline_add_completion(rs, chr_info->label);
            }
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

void ringbuf_write_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    ringbuf_completion(rs, str);
}

void device_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    peripheral_device_del_completion(rs, str, len);
}

void object_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    ObjectPropertyInfoList *list, *start;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_qom_list("/objects", NULL);
    while (list) {
        ObjectPropertyInfo *info = list->value;

        if (!strncmp(info->type, "child<", 5)
            && !strncmp(info->name, str, len)) {
            readline_add_completion(rs, info->name);
        }
        list = list->next;
    }
    qapi_free_ObjectPropertyInfoList(start);
}

void sendkey_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;
    char *sep;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    sep = strrchr(str, '-');
    if (sep) {
        str = sep + 1;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < Q_KEY_CODE__MAX; i++) {
        if (!strncmp(str, QKeyCode_str(i), len)) {
            readline_add_completion(rs, QKeyCode_str(i));
        }
    }
}

void set_link_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        NetClientState *ncs[MAX_QUEUE_NUM];
        int count, i;
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_DRIVER_NONE,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
            const char *name = ncs[i]->name;
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void netdev_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int len, count, i;
    NetClientState *ncs[MAX_QUEUE_NUM];

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    count = qemu_find_net_clients_except(NULL, ncs, NET_CLIENT_DRIVER_NIC,
                                         MAX_QUEUE_NUM);
    for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
        const char *name = ncs[i]->name;
        if (strncmp(str, name, len)) {
            continue;
        }
        if (ncs[i]->is_netdev) {
            readline_add_completion(rs, name);
        }
    }
}

void info_trace_events_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init_pattern(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    }
}

void trace_event_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init_pattern(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void watchdog_action_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;

    if (nb_args != 2) {
        return;
    }
    readline_set_completion_index(rs, strlen(str));
    for (i = 0; i < WATCHDOG_ACTION__MAX; i++) {
        add_completion_option(rs, str, WatchdogAction_str(i));
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
            const char *name = MigrationCapability_str(i);
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
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
            const char *name = MigrationParameter_str(i);
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    }
}

static void vm_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    BlockDriverState *bs;
    BdrvNextIterator it;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        SnapshotInfoList *snapshots, *snapshot;
        AioContext *ctx = bdrv_get_aio_context(bs);
        bool ok = false;

        aio_context_acquire(ctx);
        if (bdrv_can_snapshot(bs)) {
            ok = bdrv_query_snapshot_info_list(bs, &snapshots, NULL) == 0;
        }
        aio_context_release(ctx);
        if (!ok) {
            continue;
        }

        snapshot = snapshots;
        while (snapshot) {
            char *completion = snapshot->value->name;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
            completion = snapshot->value->id;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
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

static int
compare_mon_cmd(const void *a, const void *b)
{
    return strcmp(((const HMPCommand *)a)->name,
            ((const HMPCommand *)b)->name);
}

static void sortcmdlist(void)
{
    qsort(hmp_cmds, ARRAY_SIZE(hmp_cmds) - 1,
          sizeof(*hmp_cmds),
          compare_mon_cmd);
    qsort(hmp_info_cmds, ARRAY_SIZE(hmp_info_cmds) - 1,
          sizeof(*hmp_info_cmds),
          compare_mon_cmd);
}

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict))
{
    HMPCommand *table = info ? hmp_info_cmds : hmp_cmds;

    while (table->name != NULL) {
        if (strcmp(table->name, name) == 0) {
            g_assert(table->cmd == NULL && table->cmd_info_hrt == NULL);
            table->cmd = cmd;
            return;
        }
        table++;
    }
    g_assert_not_reached();
}

void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp))
{
    HMPCommand *table = hmp_info_cmds;

    while (table->name != NULL) {
        if (strcmp(table->name, name) == 0) {
            g_assert(table->cmd == NULL && table->cmd_info_hrt == NULL);
            table->cmd_info_hrt = handler;
            return;
        }
        table++;
    }
    g_assert_not_reached();
}

void monitor_init_globals(void)
{
    monitor_init_globals_core();
    monitor_init_qmp_commands();
    sortcmdlist();
    qemu_mutex_init(&mon_fdsets_lock);
}
