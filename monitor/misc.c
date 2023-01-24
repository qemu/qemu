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
#include "exec/gdbstub.h"
#include "net/slirp.h"
#include "disas/disas.h"
#include "qemu/log.h"
#include "sysemu/hw_accel.h"
#include "sysemu/sysemu.h"
#include "sysemu/device_tree.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"
#include "block/block-hmp-cmds.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-init-commands.h"
#include "qapi/error.h"
#include "qemu/cutils.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#endif

/* Make devices configuration available for use in hmp-commands*.hx templates */
#include CONFIG_DEVICES

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
    hmp_help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

static void hmp_info_help(Monitor *mon, const QDict *qdict)
{
    hmp_help_cmd(mon, "info");
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
    int vcpu = qdict_get_try_int(qdict, "vcpu", -1);
    CPUState *cs;

    if (all_cpus) {
        CPU_FOREACH(cs) {
            monitor_printf(mon, "\nCPU#%d\n", cs->cpu_index);
            cpu_dump_state(cs, NULL, CPU_DUMP_FPU);
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

static void hmp_logfile(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    if (!qemu_set_log_filename(qdict_get_str(qdict, "filename"), &err)) {
        error_report_err(err);
    }
}

static void hmp_log(Monitor *mon, const QDict *qdict)
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
            monitor_printf(mon, HWADDR_FMT_plx ":", addr);
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
    uintptr_t pagesize = qemu_real_host_page_size();
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
}
