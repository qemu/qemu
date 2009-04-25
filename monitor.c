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
#include <dirent.h>
#include "hw/hw.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "hw/watchdog.h"
#include "gdbstub.h"
#include "net.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "monitor.h"
#include "readline.h"
#include "console.h"
#include "block.h"
#include "audio/audio.h"
#include "disas.h"
#include "balloon.h"
#include "qemu-timer.h"
#include "migration.h"
#include "kvm.h"
#include "acl.h"

//#define DEBUG
//#define DEBUG_COMPLETION

/*
 * Supported types:
 *
 * 'F'          filename
 * 'B'          block device name
 * 's'          string (accept optional quote)
 * 'i'          32 bit integer
 * 'l'          target long (32 or 64 bit)
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for 'F', 's' and 'i')
 *
 */

typedef struct mon_cmd_t {
    const char *name;
    const char *args_type;
    void *handler;
    const char *params;
    const char *help;
} mon_cmd_t;

struct Monitor {
    CharDriverState *chr;
    int flags;
    int suspend_cnt;
    uint8_t outbuf[1024];
    int outbuf_index;
    ReadLineState *rs;
    CPUState *mon_cpu;
    BlockDriverCompletionFunc *password_completion_cb;
    void *password_opaque;
    LIST_ENTRY(Monitor) entry;
};

static LIST_HEAD(mon_list, Monitor) mon_list;

static const mon_cmd_t mon_cmds[];
static const mon_cmd_t info_cmds[];

Monitor *cur_mon = NULL;

static void monitor_command_cb(Monitor *mon, const char *cmdline,
                               void *opaque);

static void monitor_read_command(Monitor *mon, int show_prompt)
{
    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt)
        readline_show_prompt(mon->rs);
}

static int monitor_read_password(Monitor *mon, ReadLineFunc *readline_func,
                                 void *opaque)
{
    if (mon->rs) {
        readline_start(mon->rs, "Password: ", 1, readline_func, opaque);
        /* prompt is printed on return from the command handler */
        return 0;
    } else {
        monitor_printf(mon, "terminal does not support password prompting\n");
        return -ENOTTY;
    }
}

void monitor_flush(Monitor *mon)
{
    if (mon && mon->outbuf_index != 0 && mon->chr->focus == 0) {
        qemu_chr_write(mon->chr, mon->outbuf, mon->outbuf_index);
        mon->outbuf_index = 0;
    }
}

/* flush at every end of line or if the buffer is full */
static void monitor_puts(Monitor *mon, const char *str)
{
    char c;

    if (!mon)
        return;

    for(;;) {
        c = *str++;
        if (c == '\0')
            break;
        if (c == '\n')
            mon->outbuf[mon->outbuf_index++] = '\r';
        mon->outbuf[mon->outbuf_index++] = c;
        if (mon->outbuf_index >= (sizeof(mon->outbuf) - 1)
            || c == '\n')
            monitor_flush(mon);
    }
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    monitor_puts(mon, buf);
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    va_end(ap);
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
    int i;

    for (i = 0; filename[i]; i++) {
        switch (filename[i]) {
        case ' ':
        case '"':
        case '\\':
            monitor_printf(mon, "\\%c", filename[i]);
            break;
        case '\t':
            monitor_printf(mon, "\\t");
            break;
        case '\r':
            monitor_printf(mon, "\\r");
            break;
        case '\n':
            monitor_printf(mon, "\\n");
            break;
        default:
            monitor_printf(mon, "%c", filename[i]);
            break;
        }
    }
}

static int monitor_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf((Monitor *)stream, fmt, ap);
    va_end(ap);
    return 0;
}

static int compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        if ((p - pstart) == len && !memcmp(pstart, name, len))
            return 1;
        if (*p == '\0')
            break;
        p++;
    }
    return 0;
}

static void help_cmd_dump(Monitor *mon, const mon_cmd_t *cmds,
                          const char *prefix, const char *name)
{
    const mon_cmd_t *cmd;

    for(cmd = cmds; cmd->name != NULL; cmd++) {
        if (!name || !strcmp(name, cmd->name))
            monitor_printf(mon, "%s%s %s -- %s\n", prefix, cmd->name,
                           cmd->params, cmd->help);
    }
}

static void help_cmd(Monitor *mon, const char *name)
{
    if (name && !strcmp(name, "info")) {
        help_cmd_dump(mon, info_cmds, "info ", NULL);
    } else {
        help_cmd_dump(mon, mon_cmds, "", name);
        if (name && !strcmp(name, "log")) {
            const CPULogItem *item;
            monitor_printf(mon, "Log items (comma separated):\n");
            monitor_printf(mon, "%-10s %s\n", "none", "remove all logs");
            for(item = cpu_log_items; item->mask != 0; item++) {
                monitor_printf(mon, "%-10s %s\n", item->name, item->help);
            }
        }
    }
}

static void do_commit(Monitor *mon, const char *device)
{
    int i, all_devices;

    all_devices = !strcmp(device, "all");
    for (i = 0; i < nb_drives; i++) {
            if (all_devices ||
                !strcmp(bdrv_get_device_name(drives_table[i].bdrv), device))
                bdrv_commit(drives_table[i].bdrv);
    }
}

static void do_info(Monitor *mon, const char *item)
{
    const mon_cmd_t *cmd;
    void (*handler)(Monitor *);

    if (!item)
        goto help;
    for(cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(item, cmd->name))
            goto found;
    }
 help:
    help_cmd(mon, "info");
    return;
 found:
    handler = cmd->handler;
    handler(mon);
}

static void do_info_version(Monitor *mon)
{
    monitor_printf(mon, "%s\n", QEMU_VERSION QEMU_PKGVERSION);
}

static void do_info_name(Monitor *mon)
{
    if (qemu_name)
        monitor_printf(mon, "%s\n", qemu_name);
}

#if defined(TARGET_I386)
static void do_info_hpet(Monitor *mon)
{
    monitor_printf(mon, "HPET is %s by QEMU\n",
                   (no_hpet) ? "disabled" : "enabled");
}
#endif

static void do_info_uuid(Monitor *mon)
{
    monitor_printf(mon, UUID_FMT "\n", qemu_uuid[0], qemu_uuid[1],
                   qemu_uuid[2], qemu_uuid[3], qemu_uuid[4], qemu_uuid[5],
                   qemu_uuid[6], qemu_uuid[7], qemu_uuid[8], qemu_uuid[9],
                   qemu_uuid[10], qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                   qemu_uuid[14], qemu_uuid[15]);
}

/* get the current CPU defined by the user */
static int mon_set_cpu(int cpu_index)
{
    CPUState *env;

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        if (env->cpu_index == cpu_index) {
            cur_mon->mon_cpu = env;
            return 0;
        }
    }
    return -1;
}

static CPUState *mon_get_cpu(void)
{
    if (!cur_mon->mon_cpu) {
        mon_set_cpu(0);
    }
    cpu_synchronize_state(cur_mon->mon_cpu, 0);
    return cur_mon->mon_cpu;
}

static void do_info_registers(Monitor *mon)
{
    CPUState *env;
    env = mon_get_cpu();
    if (!env)
        return;
#ifdef TARGET_I386
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   X86_DUMP_FPU);
#else
    cpu_dump_state(env, (FILE *)mon, monitor_fprintf,
                   0);
#endif
}

static void do_info_cpus(Monitor *mon)
{
    CPUState *env;

    /* just to set the default cpu if not already done */
    mon_get_cpu();

    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        cpu_synchronize_state(env, 0);
        monitor_printf(mon, "%c CPU #%d:",
                       (env == mon->mon_cpu) ? '*' : ' ',
                       env->cpu_index);
#if defined(TARGET_I386)
        monitor_printf(mon, " pc=0x" TARGET_FMT_lx,
                       env->eip + env->segs[R_CS].base);
#elif defined(TARGET_PPC)
        monitor_printf(mon, " nip=0x" TARGET_FMT_lx, env->nip);
#elif defined(TARGET_SPARC)
        monitor_printf(mon, " pc=0x" TARGET_FMT_lx " npc=0x" TARGET_FMT_lx,
                       env->pc, env->npc);
#elif defined(TARGET_MIPS)
        monitor_printf(mon, " PC=0x" TARGET_FMT_lx, env->active_tc.PC);
#endif
        if (env->halted)
            monitor_printf(mon, " (halted)");
        monitor_printf(mon, "\n");
    }
}

static void do_cpu_set(Monitor *mon, int index)
{
    if (mon_set_cpu(index) < 0)
        monitor_printf(mon, "Invalid CPU index\n");
}

static void do_info_jit(Monitor *mon)
{
    dump_exec_info((FILE *)mon, monitor_fprintf);
}

static void do_info_history(Monitor *mon)
{
    int i;
    const char *str;

    if (!mon->rs)
        return;
    i = 0;
    for(;;) {
        str = readline_get_history(mon->rs, i);
        if (!str)
            break;
        monitor_printf(mon, "%d: '%s'\n", i, str);
        i++;
    }
}

#if defined(TARGET_PPC)
/* XXX: not implemented in other targets */
static void do_info_cpu_stats(Monitor *mon)
{
    CPUState *env;

    env = mon_get_cpu();
    cpu_dump_statistics(env, (FILE *)mon, &monitor_fprintf, 0);
}
#endif

static void do_quit(Monitor *mon)
{
    exit(0);
}

static int eject_device(Monitor *mon, BlockDriverState *bs, int force)
{
    if (bdrv_is_inserted(bs)) {
        if (!force) {
            if (!bdrv_is_removable(bs)) {
                monitor_printf(mon, "device is not removable\n");
                return -1;
            }
            if (bdrv_is_locked(bs)) {
                monitor_printf(mon, "device is locked\n");
                return -1;
            }
        }
        bdrv_close(bs);
    }
    return 0;
}

static void do_eject(Monitor *mon, int force, const char *filename)
{
    BlockDriverState *bs;

    bs = bdrv_find(filename);
    if (!bs) {
        monitor_printf(mon, "device not found\n");
        return;
    }
    eject_device(mon, bs, force);
}

static void do_change_block(Monitor *mon, const char *device,
                            const char *filename, const char *fmt)
{
    BlockDriverState *bs;
    BlockDriver *drv = NULL;

    bs = bdrv_find(device);
    if (!bs) {
        monitor_printf(mon, "device not found\n");
        return;
    }
    if (fmt) {
        drv = bdrv_find_format(fmt);
        if (!drv) {
            monitor_printf(mon, "invalid format %s\n", fmt);
            return;
        }
    }
    if (eject_device(mon, bs, 0) < 0)
        return;
    bdrv_open2(bs, filename, 0, drv);
    monitor_read_bdrv_key_start(mon, bs, NULL, NULL);
}

static void change_vnc_password_cb(Monitor *mon, const char *password,
                                   void *opaque)
{
    if (vnc_display_password(NULL, password) < 0)
        monitor_printf(mon, "could not set VNC server password\n");

    monitor_read_command(mon, 1);
}

static void do_change_vnc(Monitor *mon, const char *target, const char *arg)
{
    if (strcmp(target, "passwd") == 0 ||
        strcmp(target, "password") == 0) {
        if (arg) {
            char password[9];
            strncpy(password, arg, sizeof(password));
            password[sizeof(password) - 1] = '\0';
            change_vnc_password_cb(mon, password, NULL);
        } else {
            monitor_read_password(mon, change_vnc_password_cb, NULL);
        }
    } else {
        if (vnc_display_open(NULL, target) < 0)
            monitor_printf(mon, "could not start VNC server on %s\n", target);
    }
}

static void do_change(Monitor *mon, const char *device, const char *target,
                      const char *arg)
{
    if (strcmp(device, "vnc") == 0) {
        do_change_vnc(mon, target, arg);
    } else {
        do_change_block(mon, device, target, arg);
    }
}

static void do_screen_dump(Monitor *mon, const char *filename)
{
    vga_hw_screen_dump(filename);
}

static void do_logfile(Monitor *mon, const char *filename)
{
    cpu_set_log_filename(filename);
}

static void do_log(Monitor *mon, const char *items)
{
    int mask;

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = cpu_str_to_log_mask(items);
        if (!mask) {
            help_cmd(mon, "log");
            return;
        }
    }
    cpu_set_log(mask);
}

static void do_singlestep(Monitor *mon, const char *option)
{
    if (!option || !strcmp(option, "on")) {
        singlestep = 1;
    } else if (!strcmp(option, "off")) {
        singlestep = 0;
    } else {
        monitor_printf(mon, "unexpected option %s\n", option);
    }
}

static void do_stop(Monitor *mon)
{
    vm_stop(EXCP_INTERRUPT);
}

static void encrypted_bdrv_it(void *opaque, BlockDriverState *bs);

struct bdrv_iterate_context {
    Monitor *mon;
    int err;
};

static void do_cont(Monitor *mon)
{
    struct bdrv_iterate_context context = { mon, 0 };

    bdrv_iterate(encrypted_bdrv_it, &context);
    /* only resume the vm if all keys are set and valid */
    if (!context.err)
        vm_start();
}

static void bdrv_key_cb(void *opaque, int err)
{
    Monitor *mon = opaque;

    /* another key was set successfully, retry to continue */
    if (!err)
        do_cont(mon);
}

static void encrypted_bdrv_it(void *opaque, BlockDriverState *bs)
{
    struct bdrv_iterate_context *context = opaque;

    if (!context->err && bdrv_key_required(bs)) {
        context->err = -EBUSY;
        monitor_read_bdrv_key_start(context->mon, bs, bdrv_key_cb,
                                    context->mon);
    }
}

#ifdef CONFIG_GDBSTUB
static void do_gdbserver(Monitor *mon, const char *device)
{
    if (!device)
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
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
#endif

static void do_watchdog_action(Monitor *mon, const char *action)
{
    if (select_watchdog_action(action) == -1) {
        monitor_printf(mon, "Unknown watchdog action '%s'\n", action);
    }
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
                        target_phys_addr_t addr, int is_physical)
{
    CPUState *env;
    int nb_per_line, l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags;
        flags = 0;
        env = mon_get_cpu();
        if (!env && !is_physical)
            return;
#ifdef TARGET_I386
        if (wsize == 2) {
            flags = 1;
        } else if (wsize == 4) {
            flags = 0;
        } else {
            /* as default we use the current CS size */
            flags = 0;
            if (env) {
#ifdef TARGET_X86_64
                if ((env->efer & MSR_EFER_LMA) &&
                    (env->segs[R_CS].flags & DESC_L_MASK))
                    flags = 2;
                else
#endif
                if (!(env->segs[R_CS].flags & DESC_B_MASK))
                    flags = 1;
            }
        }
#endif
        monitor_disas(mon, env, addr, count, is_physical, flags);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    nb_per_line = line_size / wsize;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = (wsize * 8 + 2) / 3;
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = (wsize * 8 * 10 + 32) / 33;
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        if (is_physical)
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        else
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            cpu_physical_memory_rw(addr, buf, l, 0);
        } else {
            env = mon_get_cpu();
            if (!env)
                break;
            if (cpu_memory_rw_debug(env, addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_raw(buf + i);
                break;
            case 2:
                v = lduw_raw(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_raw(buf + i);
                break;
            case 8:
                v = ldq_raw(buf + i);
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

#if TARGET_LONG_BITS == 64
#define GET_TLONG(h, l) (((uint64_t)(h) << 32) | (l))
#else
#define GET_TLONG(h, l) (l)
#endif

static void do_memory_dump(Monitor *mon, int count, int format, int size,
                           uint32_t addrh, uint32_t addrl)
{
    target_long addr = GET_TLONG(addrh, addrl);
    memory_dump(mon, count, format, size, addr, 0);
}

#if TARGET_PHYS_ADDR_BITS > 32
#define GET_TPHYSADDR(h, l) (((uint64_t)(h) << 32) | (l))
#else
#define GET_TPHYSADDR(h, l) (l)
#endif

static void do_physical_memory_dump(Monitor *mon, int count, int format,
                                    int size, uint32_t addrh, uint32_t addrl)

{
    target_phys_addr_t addr = GET_TPHYSADDR(addrh, addrl);
    memory_dump(mon, count, format, size, addr, 1);
}

static void do_print(Monitor *mon, int count, int format, int size,
                     unsigned int valh, unsigned int vall)
{
    target_phys_addr_t val = GET_TPHYSADDR(valh, vall);
#if TARGET_PHYS_ADDR_BITS == 32
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#o", val);
        break;
    case 'x':
        monitor_printf(mon, "%#x", val);
        break;
    case 'u':
        monitor_printf(mon, "%u", val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%d", val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#else
    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" PRIo64, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" PRIx64, val);
        break;
    case 'u':
        monitor_printf(mon, "%" PRIu64, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" PRId64, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
#endif
    monitor_printf(mon, "\n");
}

static void do_memory_save(Monitor *mon, unsigned int valh, unsigned int vall,
                           uint32_t size, const char *filename)
{
    FILE *f;
    target_long addr = GET_TLONG(valh, vall);
    uint32_t l;
    CPUState *env;
    uint8_t buf[1024];

    env = mon_get_cpu();
    if (!env)
        return;

    f = fopen(filename, "wb");
    if (!f) {
        monitor_printf(mon, "could not open '%s'\n", filename);
        return;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_memory_rw_debug(env, addr, buf, l, 0);
        fwrite(buf, 1, l, f);
        addr += l;
        size -= l;
    }
    fclose(f);
}

static void do_physical_memory_save(Monitor *mon, unsigned int valh,
                                    unsigned int vall, uint32_t size,
                                    const char *filename)
{
    FILE *f;
    uint32_t l;
    uint8_t buf[1024];
    target_phys_addr_t addr = GET_TPHYSADDR(valh, vall); 

    f = fopen(filename, "wb");
    if (!f) {
        monitor_printf(mon, "could not open '%s'\n", filename);
        return;
    }
    while (size != 0) {
        l = sizeof(buf);
        if (l > size)
            l = size;
        cpu_physical_memory_rw(addr, buf, l, 0);
        fwrite(buf, 1, l, f);
        fflush(f);
        addr += l;
        size -= l;
    }
    fclose(f);
}

static void do_sum(Monitor *mon, uint32_t start, uint32_t size)
{
    uint32_t addr;
    uint8_t buf[1];
    uint16_t sum;

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        cpu_physical_memory_rw(addr, buf, 1, 0);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += buf[0];
    }
    monitor_printf(mon, "%05d\n", sum);
}

typedef struct {
    int keycode;
    const char *name;
} KeyDef;

static const KeyDef key_defs[] = {
    { 0x2a, "shift" },
    { 0x36, "shift_r" },

    { 0x38, "alt" },
    { 0xb8, "alt_r" },
    { 0x64, "altgr" },
    { 0xe4, "altgr_r" },
    { 0x1d, "ctrl" },
    { 0x9d, "ctrl_r" },

    { 0xdd, "menu" },

    { 0x01, "esc" },

    { 0x02, "1" },
    { 0x03, "2" },
    { 0x04, "3" },
    { 0x05, "4" },
    { 0x06, "5" },
    { 0x07, "6" },
    { 0x08, "7" },
    { 0x09, "8" },
    { 0x0a, "9" },
    { 0x0b, "0" },
    { 0x0c, "minus" },
    { 0x0d, "equal" },
    { 0x0e, "backspace" },

    { 0x0f, "tab" },
    { 0x10, "q" },
    { 0x11, "w" },
    { 0x12, "e" },
    { 0x13, "r" },
    { 0x14, "t" },
    { 0x15, "y" },
    { 0x16, "u" },
    { 0x17, "i" },
    { 0x18, "o" },
    { 0x19, "p" },

    { 0x1c, "ret" },

    { 0x1e, "a" },
    { 0x1f, "s" },
    { 0x20, "d" },
    { 0x21, "f" },
    { 0x22, "g" },
    { 0x23, "h" },
    { 0x24, "j" },
    { 0x25, "k" },
    { 0x26, "l" },

    { 0x2c, "z" },
    { 0x2d, "x" },
    { 0x2e, "c" },
    { 0x2f, "v" },
    { 0x30, "b" },
    { 0x31, "n" },
    { 0x32, "m" },
    { 0x33, "comma" },
    { 0x34, "dot" },
    { 0x35, "slash" },

    { 0x37, "asterisk" },

    { 0x39, "spc" },
    { 0x3a, "caps_lock" },
    { 0x3b, "f1" },
    { 0x3c, "f2" },
    { 0x3d, "f3" },
    { 0x3e, "f4" },
    { 0x3f, "f5" },
    { 0x40, "f6" },
    { 0x41, "f7" },
    { 0x42, "f8" },
    { 0x43, "f9" },
    { 0x44, "f10" },
    { 0x45, "num_lock" },
    { 0x46, "scroll_lock" },

    { 0xb5, "kp_divide" },
    { 0x37, "kp_multiply" },
    { 0x4a, "kp_subtract" },
    { 0x4e, "kp_add" },
    { 0x9c, "kp_enter" },
    { 0x53, "kp_decimal" },
    { 0x54, "sysrq" },

    { 0x52, "kp_0" },
    { 0x4f, "kp_1" },
    { 0x50, "kp_2" },
    { 0x51, "kp_3" },
    { 0x4b, "kp_4" },
    { 0x4c, "kp_5" },
    { 0x4d, "kp_6" },
    { 0x47, "kp_7" },
    { 0x48, "kp_8" },
    { 0x49, "kp_9" },

    { 0x56, "<" },

    { 0x57, "f11" },
    { 0x58, "f12" },

    { 0xb7, "print" },

    { 0xc7, "home" },
    { 0xc9, "pgup" },
    { 0xd1, "pgdn" },
    { 0xcf, "end" },

    { 0xcb, "left" },
    { 0xc8, "up" },
    { 0xd0, "down" },
    { 0xcd, "right" },

    { 0xd2, "insert" },
    { 0xd3, "delete" },
#if defined(TARGET_SPARC) && !defined(TARGET_SPARC64)
    { 0xf0, "stop" },
    { 0xf1, "again" },
    { 0xf2, "props" },
    { 0xf3, "undo" },
    { 0xf4, "front" },
    { 0xf5, "copy" },
    { 0xf6, "open" },
    { 0xf7, "paste" },
    { 0xf8, "find" },
    { 0xf9, "cut" },
    { 0xfa, "lf" },
    { 0xfb, "help" },
    { 0xfc, "meta_l" },
    { 0xfd, "meta_r" },
    { 0xfe, "compose" },
#endif
    { 0, NULL },
};

static int get_keycode(const char *key)
{
    const KeyDef *p;
    char *endp;
    int ret;

    for(p = key_defs; p->name != NULL; p++) {
        if (!strcmp(key, p->name))
            return p->keycode;
    }
    if (strstart(key, "0x", NULL)) {
        ret = strtoul(key, &endp, 0);
        if (*endp == '\0' && ret >= 0x01 && ret <= 0xff)
            return ret;
    }
    return -1;
}

#define MAX_KEYCODES 16
static uint8_t keycodes[MAX_KEYCODES];
static int nb_pending_keycodes;
static QEMUTimer *key_timer;

static void release_keys(void *opaque)
{
    int keycode;

    while (nb_pending_keycodes > 0) {
        nb_pending_keycodes--;
        keycode = keycodes[nb_pending_keycodes];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode | 0x80);
    }
}

static void do_sendkey(Monitor *mon, const char *string, int has_hold_time,
                       int hold_time)
{
    char keyname_buf[16];
    char *separator;
    int keyname_len, keycode, i;

    if (nb_pending_keycodes > 0) {
        qemu_del_timer(key_timer);
        release_keys(NULL);
    }
    if (!has_hold_time)
        hold_time = 100;
    i = 0;
    while (1) {
        separator = strchr(string, '-');
        keyname_len = separator ? separator - string : strlen(string);
        if (keyname_len > 0) {
            pstrcpy(keyname_buf, sizeof(keyname_buf), string);
            if (keyname_len > sizeof(keyname_buf) - 1) {
                monitor_printf(mon, "invalid key: '%s...'\n", keyname_buf);
                return;
            }
            if (i == MAX_KEYCODES) {
                monitor_printf(mon, "too many keys\n");
                return;
            }
            keyname_buf[keyname_len] = 0;
            keycode = get_keycode(keyname_buf);
            if (keycode < 0) {
                monitor_printf(mon, "unknown key: '%s'\n", keyname_buf);
                return;
            }
            keycodes[i++] = keycode;
        }
        if (!separator)
            break;
        string = separator + 1;
    }
    nb_pending_keycodes = i;
    /* key down events */
    for (i = 0; i < nb_pending_keycodes; i++) {
        keycode = keycodes[i];
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        kbd_put_keycode(keycode & 0x7f);
    }
    /* delayed key up events */
    qemu_mod_timer(key_timer, qemu_get_clock(vm_clock) +
                    muldiv64(ticks_per_sec, hold_time, 1000));
}

static int mouse_button_state;

static void do_mouse_move(Monitor *mon, const char *dx_str, const char *dy_str,
                          const char *dz_str)
{
    int dx, dy, dz;
    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    dz = 0;
    if (dz_str)
        dz = strtol(dz_str, NULL, 0);
    kbd_mouse_event(dx, dy, dz, mouse_button_state);
}

static void do_mouse_button(Monitor *mon, int button_state)
{
    mouse_button_state = button_state;
    kbd_mouse_event(0, 0, 0, mouse_button_state);
}

static void do_ioport_read(Monitor *mon, int count, int format, int size,
                           int addr, int has_index, int index)
{
    uint32_t val;
    int suffix;

    if (has_index) {
        cpu_outb(NULL, addr & 0xffff, index & 0xff);
        addr++;
    }
    addr &= 0xffff;

    switch(size) {
    default:
    case 1:
        val = cpu_inb(NULL, addr);
        suffix = 'b';
        break;
    case 2:
        val = cpu_inw(NULL, addr);
        suffix = 'w';
        break;
    case 4:
        val = cpu_inl(NULL, addr);
        suffix = 'l';
        break;
    }
    monitor_printf(mon, "port%c[0x%04x] = %#0*x\n",
                   suffix, addr, size * 2, val);
}

/* boot_set handler */
static QEMUBootSetHandler *qemu_boot_set_handler = NULL;
static void *boot_opaque;

void qemu_register_boot_set(QEMUBootSetHandler *func, void *opaque)
{
    qemu_boot_set_handler = func;
    boot_opaque = opaque;
}

static void do_boot_set(Monitor *mon, const char *bootdevice)
{
    int res;

    if (qemu_boot_set_handler)  {
        res = qemu_boot_set_handler(boot_opaque, bootdevice);
        if (res == 0)
            monitor_printf(mon, "boot device list now set to %s\n",
                           bootdevice);
        else
            monitor_printf(mon, "setting boot device list failed with "
                           "error %i\n", res);
    } else {
        monitor_printf(mon, "no function defined to set boot device list for "
                       "this architecture\n");
    }
}

static void do_system_reset(Monitor *mon)
{
    qemu_system_reset_request();
}

static void do_system_powerdown(Monitor *mon)
{
    qemu_system_powerdown_request();
}

#if defined(TARGET_I386)
static void print_pte(Monitor *mon, uint32_t addr, uint32_t pte, uint32_t mask)
{
    monitor_printf(mon, "%08x: %08x %c%c%c%c%c%c%c%c\n",
                   addr,
                   pte & mask,
                   pte & PG_GLOBAL_MASK ? 'G' : '-',
                   pte & PG_PSE_MASK ? 'P' : '-',
                   pte & PG_DIRTY_MASK ? 'D' : '-',
                   pte & PG_ACCESSED_MASK ? 'A' : '-',
                   pte & PG_PCD_MASK ? 'C' : '-',
                   pte & PG_PWT_MASK ? 'T' : '-',
                   pte & PG_USER_MASK ? 'U' : '-',
                   pte & PG_RW_MASK ? 'W' : '-');
}

static void tlb_info(Monitor *mon)
{
    CPUState *env;
    int l1, l2;
    uint32_t pgd, pde, pte;

    env = mon_get_cpu();
    if (!env)
        return;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    pgd = env->cr[3] & ~0xfff;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, (uint8_t *)&pde, 4);
        pde = le32_to_cpu(pde);
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                print_pte(mon, (l1 << 22), pde, ~((1 << 20) - 1));
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4,
                                             (uint8_t *)&pte, 4);
                    pte = le32_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, (l1 << 22) + (l2 << 12),
                                  pte & ~PG_PSE_MASK,
                                  ~0xfff);
                    }
                }
            }
        }
    }
}

static void mem_print(Monitor *mon, uint32_t *pstart, int *plast_prot,
                      uint32_t end, int prot)
{
    int prot1;
    prot1 = *plast_prot;
    if (prot != prot1) {
        if (*pstart != -1) {
            monitor_printf(mon, "%08x-%08x %08x %c%c%c\n",
                           *pstart, end, end - *pstart,
                           prot1 & PG_USER_MASK ? 'u' : '-',
                           'r',
                           prot1 & PG_RW_MASK ? 'w' : '-');
        }
        if (prot != 0)
            *pstart = end;
        else
            *pstart = -1;
        *plast_prot = prot;
    }
}

static void mem_info(Monitor *mon)
{
    CPUState *env;
    int l1, l2, prot, last_prot;
    uint32_t pgd, pde, pte, start, end;

    env = mon_get_cpu();
    if (!env)
        return;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    pgd = env->cr[3] & ~0xfff;
    last_prot = 0;
    start = -1;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, (uint8_t *)&pde, 4);
        pde = le32_to_cpu(pde);
        end = l1 << 22;
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                prot = pde & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                mem_print(mon, &start, &last_prot, end, prot);
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4,
                                             (uint8_t *)&pte, 4);
                    pte = le32_to_cpu(pte);
                    end = (l1 << 22) + (l2 << 12);
                    if (pte & PG_PRESENT_MASK) {
                        prot = pte & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                    } else {
                        prot = 0;
                    }
                    mem_print(mon, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, &start, &last_prot, end, prot);
        }
    }
}
#endif

#if defined(TARGET_SH4)

static void print_tlb(Monitor *mon, int idx, tlb_t *tlb)
{
    monitor_printf(mon, " tlb%i:\t"
                   "asid=%hhu vpn=%x\tppn=%x\tsz=%hhu size=%u\t"
                   "v=%hhu shared=%hhu cached=%hhu prot=%hhu "
                   "dirty=%hhu writethrough=%hhu\n",
                   idx,
                   tlb->asid, tlb->vpn, tlb->ppn, tlb->sz, tlb->size,
                   tlb->v, tlb->sh, tlb->c, tlb->pr,
                   tlb->d, tlb->wt);
}

static void tlb_info(Monitor *mon)
{
    CPUState *env = mon_get_cpu();
    int i;

    monitor_printf (mon, "ITLB:\n");
    for (i = 0 ; i < ITLB_SIZE ; i++)
        print_tlb (mon, i, &env->itlb[i]);
    monitor_printf (mon, "UTLB:\n");
    for (i = 0 ; i < UTLB_SIZE ; i++)
        print_tlb (mon, i, &env->utlb[i]);
}

#endif

static void do_info_kqemu(Monitor *mon)
{
#ifdef CONFIG_KQEMU
    CPUState *env;
    int val;
    val = 0;
    env = mon_get_cpu();
    if (!env) {
        monitor_printf(mon, "No cpu initialized yet");
        return;
    }
    val = env->kqemu_enabled;
    monitor_printf(mon, "kqemu support: ");
    switch(val) {
    default:
    case 0:
        monitor_printf(mon, "disabled\n");
        break;
    case 1:
        monitor_printf(mon, "enabled for user code\n");
        break;
    case 2:
        monitor_printf(mon, "enabled for user and kernel code\n");
        break;
    }
#else
    monitor_printf(mon, "kqemu support: not compiled\n");
#endif
}

static void do_info_kvm(Monitor *mon)
{
#ifdef CONFIG_KVM
    monitor_printf(mon, "kvm support: ");
    if (kvm_enabled())
        monitor_printf(mon, "enabled\n");
    else
        monitor_printf(mon, "disabled\n");
#else
    monitor_printf(mon, "kvm support: not compiled\n");
#endif
}

static void do_info_numa(Monitor *mon)
{
    int i;
    CPUState *env;

    monitor_printf(mon, "%d nodes\n", nb_numa_nodes);
    for (i = 0; i < nb_numa_nodes; i++) {
        monitor_printf(mon, "node %d cpus:", i);
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (env->numa_node == i) {
                monitor_printf(mon, " %d", env->cpu_index);
            }
        }
        monitor_printf(mon, "\n");
        monitor_printf(mon, "node %d size: %" PRId64 " MB\n", i,
            node_mem[i] >> 20);
    }
}

#ifdef CONFIG_PROFILER

int64_t kqemu_time;
int64_t qemu_time;
int64_t kqemu_exec_count;
int64_t dev_time;
int64_t kqemu_ret_int_count;
int64_t kqemu_ret_excp_count;
int64_t kqemu_ret_intr_count;

static void do_info_profile(Monitor *mon)
{
    int64_t total;
    total = qemu_time;
    if (total == 0)
        total = 1;
    monitor_printf(mon, "async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double)ticks_per_sec);
    monitor_printf(mon, "qemu time   %" PRId64 " (%0.3f)\n",
                   qemu_time, qemu_time / (double)ticks_per_sec);
    monitor_printf(mon, "kqemu time  %" PRId64 " (%0.3f %0.1f%%) count=%"
                        PRId64 " int=%" PRId64 " excp=%" PRId64 " intr=%"
                        PRId64 "\n",
                   kqemu_time, kqemu_time / (double)ticks_per_sec,
                   kqemu_time / (double)total * 100.0,
                   kqemu_exec_count,
                   kqemu_ret_int_count,
                   kqemu_ret_excp_count,
                   kqemu_ret_intr_count);
    qemu_time = 0;
    kqemu_time = 0;
    kqemu_exec_count = 0;
    dev_time = 0;
    kqemu_ret_int_count = 0;
    kqemu_ret_excp_count = 0;
    kqemu_ret_intr_count = 0;
#ifdef CONFIG_KQEMU
    kqemu_record_dump();
#endif
}
#else
static void do_info_profile(Monitor *mon)
{
    monitor_printf(mon, "Internal profiler not compiled\n");
}
#endif

/* Capture support */
static LIST_HEAD (capture_list_head, CaptureState) capture_head;

static void do_info_capture(Monitor *mon)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

static void do_stop_capture(Monitor *mon, int n)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            LIST_REMOVE (s, entries);
            qemu_free (s);
            return;
        }
    }
}

#ifdef HAS_AUDIO
static void do_wav_capture(Monitor *mon, const char *path,
                           int has_freq, int freq,
                           int has_bits, int bits,
                           int has_channels, int nchannels)
{
    CaptureState *s;

    s = qemu_mallocz (sizeof (*s));

    freq = has_freq ? freq : 44100;
    bits = has_bits ? bits : 16;
    nchannels = has_channels ? nchannels : 2;

    if (wav_start_capture (s, path, freq, bits, nchannels)) {
        monitor_printf(mon, "Faied to add wave capture\n");
        qemu_free (s);
    }
    LIST_INSERT_HEAD (&capture_head, s, entries);
}
#endif

#if defined(TARGET_I386)
static void do_inject_nmi(Monitor *mon, int cpu_index)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu)
        if (env->cpu_index == cpu_index) {
            cpu_interrupt(env, CPU_INTERRUPT_NMI);
            break;
        }
}
#endif

static void do_info_status(Monitor *mon)
{
    if (vm_running) {
        if (singlestep) {
            monitor_printf(mon, "VM status: running (single step mode)\n");
        } else {
            monitor_printf(mon, "VM status: running\n");
        }
    } else
       monitor_printf(mon, "VM status: paused\n");
}


static void do_balloon(Monitor *mon, int value)
{
    ram_addr_t target = value;
    qemu_balloon(target << 20);
}

static void do_info_balloon(Monitor *mon)
{
    ram_addr_t actual;

    actual = qemu_balloon_status();
    if (kvm_enabled() && !kvm_has_sync_mmu())
        monitor_printf(mon, "Using KVM without synchronous MMU, "
                       "ballooning disabled\n");
    else if (actual == 0)
        monitor_printf(mon, "Ballooning not activated in VM\n");
    else
        monitor_printf(mon, "balloon: actual=%d\n", (int)(actual >> 20));
}

static void do_acl(Monitor *mon,
                   const char *command,
                   const char *aclname,
                   const char *match,
                   int has_index,
                   int index)
{
    qemu_acl *acl;

    acl = qemu_acl_find(aclname);
    if (!acl) {
        monitor_printf(mon, "acl: unknown list '%s'\n", aclname);
        return;
    }

    if (strcmp(command, "show") == 0) {
        int i = 0;
        qemu_acl_entry *entry;
        monitor_printf(mon, "policy: %s\n",
                       acl->defaultDeny ? "deny" : "allow");
        TAILQ_FOREACH(entry, &acl->entries, next) {
            i++;
            monitor_printf(mon, "%d: %s %s\n", i,
                           entry->deny ? "deny" : "allow",
                           entry->match);
        }
    } else if (strcmp(command, "reset") == 0) {
        qemu_acl_reset(acl);
        monitor_printf(mon, "acl: removed all rules\n");
    } else if (strcmp(command, "policy") == 0) {
        if (!match) {
            monitor_printf(mon, "acl: missing policy parameter\n");
            return;
        }

        if (strcmp(match, "allow") == 0) {
            acl->defaultDeny = 0;
            monitor_printf(mon, "acl: policy set to 'allow'\n");
        } else if (strcmp(match, "deny") == 0) {
            acl->defaultDeny = 1;
            monitor_printf(mon, "acl: policy set to 'deny'\n");
        } else {
            monitor_printf(mon, "acl: unknown policy '%s', expected 'deny' or 'allow'\n", match);
        }
    } else if ((strcmp(command, "allow") == 0) ||
               (strcmp(command, "deny") == 0)) {
        int deny = strcmp(command, "deny") == 0 ? 1 : 0;
        int ret;

        if (!match) {
            monitor_printf(mon, "acl: missing match parameter\n");
            return;
        }

        if (has_index)
            ret = qemu_acl_insert(acl, deny, match, index);
        else
            ret = qemu_acl_append(acl, deny, match);
        if (ret < 0)
            monitor_printf(mon, "acl: unable to add acl entry\n");
        else
            monitor_printf(mon, "acl: added rule at position %d\n", ret);
    } else if (strcmp(command, "remove") == 0) {
        int ret;

        if (!match) {
            monitor_printf(mon, "acl: missing match parameter\n");
            return;
        }

        ret = qemu_acl_remove(acl, match);
        if (ret < 0)
            monitor_printf(mon, "acl: no matching acl entry\n");
        else
            monitor_printf(mon, "acl: removed rule at position %d\n", ret);
    } else {
        monitor_printf(mon, "acl: unknown command '%s'\n", command);
    }
}

/* Please update qemu-doc.texi when adding or changing commands */
static const mon_cmd_t mon_cmds[] = {
    { "help|?", "s?", help_cmd,
      "[cmd]", "show the help" },
    { "commit", "s", do_commit,
      "device|all", "commit changes to the disk images (if -snapshot is used) or backing files" },
    { "info", "s?", do_info,
      "[subcommand]", "show various information about the system state" },
    { "q|quit", "", do_quit,
      "", "quit the emulator" },
    { "eject", "-fB", do_eject,
      "[-f] device", "eject a removable medium (use -f to force it)" },
    { "change", "BFs?", do_change,
      "device filename [format]", "change a removable medium, optional format" },
    { "screendump", "F", do_screen_dump,
      "filename", "save screen into PPM image 'filename'" },
    { "logfile", "F", do_logfile,
      "filename", "output logs to 'filename'" },
    { "log", "s", do_log,
      "item1[,...]", "activate logging of the specified items to '/tmp/qemu.log'" },
    { "savevm", "s?", do_savevm,
      "[tag|id]", "save a VM snapshot. If no tag or id are provided, a new snapshot is created" },
    { "loadvm", "s", do_loadvm,
      "tag|id", "restore a VM snapshot from its tag or id" },
    { "delvm", "s", do_delvm,
      "tag|id", "delete a VM snapshot from its tag or id" },
    { "singlestep", "s?", do_singlestep,
      "[on|off]", "run emulation in singlestep mode or switch to normal mode", },
    { "stop", "", do_stop,
      "", "stop emulation", },
    { "c|cont", "", do_cont,
      "", "resume emulation", },
#ifdef CONFIG_GDBSTUB
    { "gdbserver", "s?", do_gdbserver,
      "[device]", "start gdbserver on given device (default 'tcp::1234'), stop with 'none'", },
#endif
    { "x", "/l", do_memory_dump,
      "/fmt addr", "virtual memory dump starting at 'addr'", },
    { "xp", "/l", do_physical_memory_dump,
      "/fmt addr", "physical memory dump starting at 'addr'", },
    { "p|print", "/l", do_print,
      "/fmt expr", "print expression value (use $reg for CPU register access)", },
    { "i", "/ii.", do_ioport_read,
      "/fmt addr", "I/O port read" },

    { "sendkey", "si?", do_sendkey,
      "keys [hold_ms]", "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)" },
    { "system_reset", "", do_system_reset,
      "", "reset the system" },
    { "system_powerdown", "", do_system_powerdown,
      "", "send system power down event" },
    { "sum", "ii", do_sum,
      "addr size", "compute the checksum of a memory region" },
    { "usb_add", "s", do_usb_add,
      "device", "add USB device (e.g. 'host:bus.addr' or 'host:vendor_id:product_id')" },
    { "usb_del", "s", do_usb_del,
      "device", "remove USB device 'bus.addr'" },
    { "cpu", "i", do_cpu_set,
      "index", "set the default CPU" },
    { "mouse_move", "sss?", do_mouse_move,
      "dx dy [dz]", "send mouse move events" },
    { "mouse_button", "i", do_mouse_button,
      "state", "change mouse button state (1=L, 2=M, 4=R)" },
    { "mouse_set", "i", do_mouse_set,
      "index", "set which mouse device receives events" },
#ifdef HAS_AUDIO
    { "wavcapture", "si?i?i?", do_wav_capture,
      "path [frequency [bits [channels]]]",
      "capture audio to a wave file (default frequency=44100 bits=16 channels=2)" },
#endif
    { "stopcapture", "i", do_stop_capture,
      "capture index", "stop capture" },
    { "memsave", "lis", do_memory_save,
      "addr size file", "save to disk virtual memory dump starting at 'addr' of size 'size'", },
    { "pmemsave", "lis", do_physical_memory_save,
      "addr size file", "save to disk physical memory dump starting at 'addr' of size 'size'", },
    { "boot_set", "s", do_boot_set,
      "bootdevice", "define new values for the boot device list" },
#if defined(TARGET_I386)
    { "nmi", "i", do_inject_nmi,
      "cpu", "inject an NMI on the given CPU", },
#endif
    { "migrate", "-ds", do_migrate,
      "[-d] uri", "migrate to URI (using -d to not wait for completion)" },
    { "migrate_cancel", "", do_migrate_cancel,
      "", "cancel the current VM migration" },
    { "migrate_set_speed", "s", do_migrate_set_speed,
      "value", "set maximum speed (in bytes) for migrations" },
#if defined(TARGET_I386)
    { "drive_add", "ss", drive_hot_add, "pci_addr=[[<domain>:]<bus>:]<slot>\n"
                                         "[file=file][,if=type][,bus=n]\n"
                                        "[,unit=m][,media=d][index=i]\n"
                                        "[,cyls=c,heads=h,secs=s[,trans=t]]\n"
                                        "[snapshot=on|off][,cache=on|off]",
                                        "add drive to PCI storage controller" },
    { "pci_add", "sss", pci_device_hot_add, "pci_addr=auto|[[<domain>:]<bus>:]<slot> nic|storage [[vlan=n][,macaddr=addr][,model=type]] [file=file][,if=type][,bus=nr]...", "hot-add PCI device" },
    { "pci_del", "s", pci_device_hot_remove, "pci_addr=[[<domain>:]<bus>:]<slot>", "hot remove PCI device" },
#endif
    { "host_net_add", "ss?", net_host_device_add,
      "tap|user|socket|vde|dump [options]", "add host VLAN client" },
    { "host_net_remove", "is", net_host_device_remove,
      "vlan_id name", "remove host VLAN client" },
#ifdef CONFIG_SLIRP
    { "host_net_redir", "s", net_slirp_redir,
      "[tcp|udp]:host-port:[guest-host]:guest-port", "redirect TCP or UDP connections from host to guest (requires -net user)" },
#endif
    { "balloon", "i", do_balloon,
      "target", "request VM to change it's memory allocation (in MB)" },
    { "set_link", "ss", do_set_link,
      "name up|down", "change the link status of a network adapter" },
    { "watchdog_action", "s", do_watchdog_action,
      "[reset|shutdown|poweroff|pause|debug|none]", "change watchdog action" },
    { "acl", "sss?i?", do_acl, "<command> <aclname> [<match> [<index>]]\n",
                               "acl show vnc.username\n"
                               "acl policy vnc.username deny\n"
                               "acl allow vnc.username fred\n"
                               "acl deny vnc.username bob\n"
                               "acl reset vnc.username\n" },
    { NULL, NULL, },
};

/* Please update qemu-doc.texi when adding or changing commands */
static const mon_cmd_t info_cmds[] = {
    { "version", "", do_info_version,
      "", "show the version of QEMU" },
    { "network", "", do_info_network,
      "", "show the network state" },
    { "chardev", "", qemu_chr_info,
      "", "show the character devices" },
    { "block", "", bdrv_info,
      "", "show the block devices" },
    { "blockstats", "", bdrv_info_stats,
      "", "show block device statistics" },
    { "registers", "", do_info_registers,
      "", "show the cpu registers" },
    { "cpus", "", do_info_cpus,
      "", "show infos for each CPU" },
    { "history", "", do_info_history,
      "", "show the command line history", },
    { "irq", "", irq_info,
      "", "show the interrupts statistics (if available)", },
    { "pic", "", pic_info,
      "", "show i8259 (PIC) state", },
    { "pci", "", pci_info,
      "", "show PCI info", },
#if defined(TARGET_I386) || defined(TARGET_SH4)
    { "tlb", "", tlb_info,
      "", "show virtual to physical memory mappings", },
#endif
#if defined(TARGET_I386)
    { "mem", "", mem_info,
      "", "show the active virtual memory mappings", },
    { "hpet", "", do_info_hpet,
      "", "show state of HPET", },
#endif
    { "jit", "", do_info_jit,
      "", "show dynamic compiler info", },
    { "kqemu", "", do_info_kqemu,
      "", "show KQEMU information", },
    { "kvm", "", do_info_kvm,
      "", "show KVM information", },
    { "numa", "", do_info_numa,
      "", "show NUMA information", },
    { "usb", "", usb_info,
      "", "show guest USB devices", },
    { "usbhost", "", usb_host_info,
      "", "show host USB devices", },
    { "profile", "", do_info_profile,
      "", "show profiling information", },
    { "capture", "", do_info_capture,
      "", "show capture information" },
    { "snapshots", "", do_info_snapshots,
      "", "show the currently saved VM snapshots" },
    { "status", "", do_info_status,
      "", "show the current VM status (running|paused)" },
    { "pcmcia", "", pcmcia_info,
      "", "show guest PCMCIA status" },
    { "mice", "", do_info_mice,
      "", "show which guest mouse is receiving events" },
    { "vnc", "", do_info_vnc,
      "", "show the vnc server status"},
    { "name", "", do_info_name,
      "", "show the current VM name" },
    { "uuid", "", do_info_uuid,
      "", "show the current VM UUID" },
#if defined(TARGET_PPC)
    { "cpustats", "", do_info_cpu_stats,
      "", "show CPU statistics", },
#endif
#if defined(CONFIG_SLIRP)
    { "slirp", "", do_info_slirp,
      "", "show SLIRP statistics", },
#endif
    { "migrate", "", do_info_migrate, "", "show migration status" },
    { "balloon", "", do_info_balloon,
      "", "show balloon information" },
    { NULL, NULL, },
};

/*******************************************************************/

static const char *pch;
static jmp_buf expr_env;

#define MD_TLONG 0
#define MD_I32   1

typedef struct MonitorDef {
    const char *name;
    int offset;
    target_long (*get_value)(const struct MonitorDef *md, int val);
    int type;
} MonitorDef;

#if defined(TARGET_I386)
static target_long monitor_get_pc (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return env->eip + env->segs[R_CS].base;
}
#endif

#if defined(TARGET_PPC)
static target_long monitor_get_ccr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    unsigned int u;
    int i;

    if (!env)
        return 0;

    u = 0;
    for (i = 0; i < 8; i++)
        u |= env->crf[i] << (32 - (4 * i));

    return u;
}

static target_long monitor_get_msr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return env->msr;
}

static target_long monitor_get_xer (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return env->xer;
}

static target_long monitor_get_decr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return cpu_ppc_load_tbl(env);
}
#endif

#if defined(TARGET_SPARC)
#ifndef TARGET_SPARC64
static target_long monitor_get_psr (const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return GET_PSR(env);
}
#endif

static target_long monitor_get_reg(const struct MonitorDef *md, int val)
{
    CPUState *env = mon_get_cpu();
    if (!env)
        return 0;
    return env->regwptr[val];
}
#endif

static const MonitorDef monitor_defs[] = {
#ifdef TARGET_I386

#define SEG(name, seg) \
    { name, offsetof(CPUState, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUState, segs[seg].base) },\
    { name ".limit", offsetof(CPUState, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUState, regs[0]) },
    { "ecx", offsetof(CPUState, regs[1]) },
    { "edx", offsetof(CPUState, regs[2]) },
    { "ebx", offsetof(CPUState, regs[3]) },
    { "esp|sp", offsetof(CPUState, regs[4]) },
    { "ebp|fp", offsetof(CPUState, regs[5]) },
    { "esi", offsetof(CPUState, regs[6]) },
    { "edi", offsetof(CPUState, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUState, regs[8]) },
    { "r9", offsetof(CPUState, regs[9]) },
    { "r10", offsetof(CPUState, regs[10]) },
    { "r11", offsetof(CPUState, regs[11]) },
    { "r12", offsetof(CPUState, regs[12]) },
    { "r13", offsetof(CPUState, regs[13]) },
    { "r14", offsetof(CPUState, regs[14]) },
    { "r15", offsetof(CPUState, regs[15]) },
#endif
    { "eflags", offsetof(CPUState, eflags) },
    { "eip", offsetof(CPUState, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
#elif defined(TARGET_PPC)
    /* General purpose registers */
    { "r0", offsetof(CPUState, gpr[0]) },
    { "r1", offsetof(CPUState, gpr[1]) },
    { "r2", offsetof(CPUState, gpr[2]) },
    { "r3", offsetof(CPUState, gpr[3]) },
    { "r4", offsetof(CPUState, gpr[4]) },
    { "r5", offsetof(CPUState, gpr[5]) },
    { "r6", offsetof(CPUState, gpr[6]) },
    { "r7", offsetof(CPUState, gpr[7]) },
    { "r8", offsetof(CPUState, gpr[8]) },
    { "r9", offsetof(CPUState, gpr[9]) },
    { "r10", offsetof(CPUState, gpr[10]) },
    { "r11", offsetof(CPUState, gpr[11]) },
    { "r12", offsetof(CPUState, gpr[12]) },
    { "r13", offsetof(CPUState, gpr[13]) },
    { "r14", offsetof(CPUState, gpr[14]) },
    { "r15", offsetof(CPUState, gpr[15]) },
    { "r16", offsetof(CPUState, gpr[16]) },
    { "r17", offsetof(CPUState, gpr[17]) },
    { "r18", offsetof(CPUState, gpr[18]) },
    { "r19", offsetof(CPUState, gpr[19]) },
    { "r20", offsetof(CPUState, gpr[20]) },
    { "r21", offsetof(CPUState, gpr[21]) },
    { "r22", offsetof(CPUState, gpr[22]) },
    { "r23", offsetof(CPUState, gpr[23]) },
    { "r24", offsetof(CPUState, gpr[24]) },
    { "r25", offsetof(CPUState, gpr[25]) },
    { "r26", offsetof(CPUState, gpr[26]) },
    { "r27", offsetof(CPUState, gpr[27]) },
    { "r28", offsetof(CPUState, gpr[28]) },
    { "r29", offsetof(CPUState, gpr[29]) },
    { "r30", offsetof(CPUState, gpr[30]) },
    { "r31", offsetof(CPUState, gpr[31]) },
    /* Floating point registers */
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
    { "fpscr", offsetof(CPUState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUState, nip) },
    { "lr", offsetof(CPUState, lr) },
    { "ctr", offsetof(CPUState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "msr", 0, &monitor_get_msr, },
    { "xer", 0, &monitor_get_xer, },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
#if defined(TARGET_PPC64)
    /* Address space register */
    { "asr", offsetof(CPUState, asr) },
#endif
    /* Segment registers */
    { "sdr1", offsetof(CPUState, sdr1) },
    { "sr0", offsetof(CPUState, sr[0]) },
    { "sr1", offsetof(CPUState, sr[1]) },
    { "sr2", offsetof(CPUState, sr[2]) },
    { "sr3", offsetof(CPUState, sr[3]) },
    { "sr4", offsetof(CPUState, sr[4]) },
    { "sr5", offsetof(CPUState, sr[5]) },
    { "sr6", offsetof(CPUState, sr[6]) },
    { "sr7", offsetof(CPUState, sr[7]) },
    { "sr8", offsetof(CPUState, sr[8]) },
    { "sr9", offsetof(CPUState, sr[9]) },
    { "sr10", offsetof(CPUState, sr[10]) },
    { "sr11", offsetof(CPUState, sr[11]) },
    { "sr12", offsetof(CPUState, sr[12]) },
    { "sr13", offsetof(CPUState, sr[13]) },
    { "sr14", offsetof(CPUState, sr[14]) },
    { "sr15", offsetof(CPUState, sr[15]) },
    /* Too lazy to put BATs and SPRs ... */
#elif defined(TARGET_SPARC)
    { "g0", offsetof(CPUState, gregs[0]) },
    { "g1", offsetof(CPUState, gregs[1]) },
    { "g2", offsetof(CPUState, gregs[2]) },
    { "g3", offsetof(CPUState, gregs[3]) },
    { "g4", offsetof(CPUState, gregs[4]) },
    { "g5", offsetof(CPUState, gregs[5]) },
    { "g6", offsetof(CPUState, gregs[6]) },
    { "g7", offsetof(CPUState, gregs[7]) },
    { "o0", 0, monitor_get_reg },
    { "o1", 1, monitor_get_reg },
    { "o2", 2, monitor_get_reg },
    { "o3", 3, monitor_get_reg },
    { "o4", 4, monitor_get_reg },
    { "o5", 5, monitor_get_reg },
    { "o6", 6, monitor_get_reg },
    { "o7", 7, monitor_get_reg },
    { "l0", 8, monitor_get_reg },
    { "l1", 9, monitor_get_reg },
    { "l2", 10, monitor_get_reg },
    { "l3", 11, monitor_get_reg },
    { "l4", 12, monitor_get_reg },
    { "l5", 13, monitor_get_reg },
    { "l6", 14, monitor_get_reg },
    { "l7", 15, monitor_get_reg },
    { "i0", 16, monitor_get_reg },
    { "i1", 17, monitor_get_reg },
    { "i2", 18, monitor_get_reg },
    { "i3", 19, monitor_get_reg },
    { "i4", 20, monitor_get_reg },
    { "i5", 21, monitor_get_reg },
    { "i6", 22, monitor_get_reg },
    { "i7", 23, monitor_get_reg },
    { "pc", offsetof(CPUState, pc) },
    { "npc", offsetof(CPUState, npc) },
    { "y", offsetof(CPUState, y) },
#ifndef TARGET_SPARC64
    { "psr", 0, &monitor_get_psr, },
    { "wim", offsetof(CPUState, wim) },
#endif
    { "tbr", offsetof(CPUState, tbr) },
    { "fsr", offsetof(CPUState, fsr) },
    { "f0", offsetof(CPUState, fpr[0]) },
    { "f1", offsetof(CPUState, fpr[1]) },
    { "f2", offsetof(CPUState, fpr[2]) },
    { "f3", offsetof(CPUState, fpr[3]) },
    { "f4", offsetof(CPUState, fpr[4]) },
    { "f5", offsetof(CPUState, fpr[5]) },
    { "f6", offsetof(CPUState, fpr[6]) },
    { "f7", offsetof(CPUState, fpr[7]) },
    { "f8", offsetof(CPUState, fpr[8]) },
    { "f9", offsetof(CPUState, fpr[9]) },
    { "f10", offsetof(CPUState, fpr[10]) },
    { "f11", offsetof(CPUState, fpr[11]) },
    { "f12", offsetof(CPUState, fpr[12]) },
    { "f13", offsetof(CPUState, fpr[13]) },
    { "f14", offsetof(CPUState, fpr[14]) },
    { "f15", offsetof(CPUState, fpr[15]) },
    { "f16", offsetof(CPUState, fpr[16]) },
    { "f17", offsetof(CPUState, fpr[17]) },
    { "f18", offsetof(CPUState, fpr[18]) },
    { "f19", offsetof(CPUState, fpr[19]) },
    { "f20", offsetof(CPUState, fpr[20]) },
    { "f21", offsetof(CPUState, fpr[21]) },
    { "f22", offsetof(CPUState, fpr[22]) },
    { "f23", offsetof(CPUState, fpr[23]) },
    { "f24", offsetof(CPUState, fpr[24]) },
    { "f25", offsetof(CPUState, fpr[25]) },
    { "f26", offsetof(CPUState, fpr[26]) },
    { "f27", offsetof(CPUState, fpr[27]) },
    { "f28", offsetof(CPUState, fpr[28]) },
    { "f29", offsetof(CPUState, fpr[29]) },
    { "f30", offsetof(CPUState, fpr[30]) },
    { "f31", offsetof(CPUState, fpr[31]) },
#ifdef TARGET_SPARC64
    { "f32", offsetof(CPUState, fpr[32]) },
    { "f34", offsetof(CPUState, fpr[34]) },
    { "f36", offsetof(CPUState, fpr[36]) },
    { "f38", offsetof(CPUState, fpr[38]) },
    { "f40", offsetof(CPUState, fpr[40]) },
    { "f42", offsetof(CPUState, fpr[42]) },
    { "f44", offsetof(CPUState, fpr[44]) },
    { "f46", offsetof(CPUState, fpr[46]) },
    { "f48", offsetof(CPUState, fpr[48]) },
    { "f50", offsetof(CPUState, fpr[50]) },
    { "f52", offsetof(CPUState, fpr[52]) },
    { "f54", offsetof(CPUState, fpr[54]) },
    { "f56", offsetof(CPUState, fpr[56]) },
    { "f58", offsetof(CPUState, fpr[58]) },
    { "f60", offsetof(CPUState, fpr[60]) },
    { "f62", offsetof(CPUState, fpr[62]) },
    { "asi", offsetof(CPUState, asi) },
    { "pstate", offsetof(CPUState, pstate) },
    { "cansave", offsetof(CPUState, cansave) },
    { "canrestore", offsetof(CPUState, canrestore) },
    { "otherwin", offsetof(CPUState, otherwin) },
    { "wstate", offsetof(CPUState, wstate) },
    { "cleanwin", offsetof(CPUState, cleanwin) },
    { "fprs", offsetof(CPUState, fprs) },
#endif
#endif
    { NULL },
};

static void expr_error(Monitor *mon, const char *msg)
{
    monitor_printf(mon, "%s\n", msg);
    longjmp(expr_env, 1);
}

/* return 0 if OK, -1 if not found, -2 if no CPU defined */
static int get_monitor_def(target_long *pval, const char *name)
{
    const MonitorDef *md;
    void *ptr;

    for(md = monitor_defs; md->name != NULL; md++) {
        if (compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md, md->offset);
            } else {
                CPUState *env = mon_get_cpu();
                if (!env)
                    return -2;
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
    return -1;
}

static void next(void)
{
    if (pch != '\0') {
        pch++;
        while (qemu_isspace(*pch))
            pch++;
    }
}

static int64_t expr_sum(Monitor *mon);

static int64_t expr_unary(Monitor *mon)
{
    int64_t n;
    char *p;
    int ret;

    switch(*pch) {
    case '+':
        next();
        n = expr_unary(mon);
        break;
    case '-':
        next();
        n = -expr_unary(mon);
        break;
    case '~':
        next();
        n = ~expr_unary(mon);
        break;
    case '(':
        next();
        n = expr_sum(mon);
        if (*pch != ')') {
            expr_error(mon, "')' expected");
        }
        next();
        break;
    case '\'':
        pch++;
        if (*pch == '\0')
            expr_error(mon, "character constant expected");
        n = *pch;
        pch++;
        if (*pch != '\'')
            expr_error(mon, "missing terminating \' character");
        next();
        break;
    case '$':
        {
            char buf[128], *q;
            target_long reg=0;

            pch++;
            q = buf;
            while ((*pch >= 'a' && *pch <= 'z') ||
                   (*pch >= 'A' && *pch <= 'Z') ||
                   (*pch >= '0' && *pch <= '9') ||
                   *pch == '_' || *pch == '.') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *pch;
                pch++;
            }
            while (qemu_isspace(*pch))
                pch++;
            *q = 0;
            ret = get_monitor_def(&reg, buf);
            if (ret == -1)
                expr_error(mon, "unknown register");
            else if (ret == -2)
                expr_error(mon, "no cpu defined");
            n = reg;
        }
        break;
    case '\0':
        expr_error(mon, "unexpected end of expression");
        n = 0;
        break;
    default:
#if TARGET_PHYS_ADDR_BITS > 32
        n = strtoull(pch, &p, 0);
#else
        n = strtoul(pch, &p, 0);
#endif
        if (pch == p) {
            expr_error(mon, "invalid char in expression");
        }
        pch = p;
        while (qemu_isspace(*pch))
            pch++;
        break;
    }
    return n;
}


static int64_t expr_prod(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_unary(mon);
    for(;;) {
        op = *pch;
        if (op != '*' && op != '/' && op != '%')
            break;
        next();
        val2 = expr_unary(mon);
        switch(op) {
        default:
        case '*':
            val *= val2;
            break;
        case '/':
        case '%':
            if (val2 == 0)
                expr_error(mon, "division by zero");
            if (op == '/')
                val /= val2;
            else
                val %= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_logic(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_prod(mon);
    for(;;) {
        op = *pch;
        if (op != '&' && op != '|' && op != '^')
            break;
        next();
        val2 = expr_prod(mon);
        switch(op) {
        default:
        case '&':
            val &= val2;
            break;
        case '|':
            val |= val2;
            break;
        case '^':
            val ^= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_sum(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_logic(mon);
    for(;;) {
        op = *pch;
        if (op != '+' && op != '-')
            break;
        next();
        val2 = expr_logic(mon);
        if (op == '+')
            val += val2;
        else
            val -= val2;
    }
    return val;
}

static int get_expr(Monitor *mon, int64_t *pval, const char **pp)
{
    pch = *pp;
    if (setjmp(expr_env)) {
        *pp = pch;
        return -1;
    }
    while (qemu_isspace(*pch))
        pch++;
    *pval = expr_sum(mon);
    *pp = pch;
    return 0;
}

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    q = buf;
    p = *pp;
    while (qemu_isspace(*p))
        p++;
    if (*p == '\0') {
    fail:
        *q = '\0';
        *pp = p;
        return -1;
    }
    if (*p == '\"') {
        p++;
        while (*p != '\0' && *p != '\"') {
            if (*p == '\\') {
                p++;
                c = *p++;
                switch(c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    qemu_printf("unsupported escape code: '\\%c'\n", c);
                    goto fail;
                }
                if ((q - buf) < buf_size - 1) {
                    *q++ = c;
                }
            } else {
                if ((q - buf) < buf_size - 1) {
                    *q++ = *p;
                }
                p++;
            }
        }
        if (*p != '\"') {
            qemu_printf("unterminated string\n");
            goto fail;
        }
        p++;
    } else {
        while (*p != '\0' && !qemu_isspace(*p)) {
            if ((q - buf) < buf_size - 1) {
                *q++ = *p;
            }
            p++;
        }
    }
    *q = '\0';
    *pp = p;
    return 0;
}

static int default_fmt_format = 'x';
static int default_fmt_size = 4;

#define MAX_ARGS 16

static void monitor_handle_command(Monitor *mon, const char *cmdline)
{
    const char *p, *pstart, *typestr;
    char *q;
    int c, nb_args, len, i, has_arg;
    const mon_cmd_t *cmd;
    char cmdname[256];
    char buf[1024];
    void *str_allocated[MAX_ARGS];
    void *args[MAX_ARGS];
    void (*handler_0)(Monitor *mon);
    void (*handler_1)(Monitor *mon, void *arg0);
    void (*handler_2)(Monitor *mon, void *arg0, void *arg1);
    void (*handler_3)(Monitor *mon, void *arg0, void *arg1, void *arg2);
    void (*handler_4)(Monitor *mon, void *arg0, void *arg1, void *arg2,
                      void *arg3);
    void (*handler_5)(Monitor *mon, void *arg0, void *arg1, void *arg2,
                      void *arg3, void *arg4);
    void (*handler_6)(Monitor *mon, void *arg0, void *arg1, void *arg2,
                      void *arg3, void *arg4, void *arg5);
    void (*handler_7)(Monitor *mon, void *arg0, void *arg1, void *arg2,
                      void *arg3, void *arg4, void *arg5, void *arg6);

#ifdef DEBUG
    monitor_printf(mon, "command='%s'\n", cmdline);
#endif

    /* extract the command name */
    p = cmdline;
    q = cmdname;
    while (qemu_isspace(*p))
        p++;
    if (*p == '\0')
        return;
    pstart = p;
    while (*p != '\0' && *p != '/' && !qemu_isspace(*p))
        p++;
    len = p - pstart;
    if (len > sizeof(cmdname) - 1)
        len = sizeof(cmdname) - 1;
    memcpy(cmdname, pstart, len);
    cmdname[len] = '\0';

    /* find the command */
    for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(cmdname, cmd->name))
            goto found;
    }
    monitor_printf(mon, "unknown command: '%s'\n", cmdname);
    return;
 found:

    for(i = 0; i < MAX_ARGS; i++)
        str_allocated[i] = NULL;

    /* parse the parameters */
    typestr = cmd->args_type;
    nb_args = 0;
    for(;;) {
        c = *typestr;
        if (c == '\0')
            break;
        typestr++;
        switch(c) {
        case 'F':
        case 'B':
        case 's':
            {
                int ret;
                char *str;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no optional string: NULL argument */
                        str = NULL;
                        goto add_str;
                    }
                }
                ret = get_str(buf, sizeof(buf), &p);
                if (ret < 0) {
                    switch(c) {
                    case 'F':
                        monitor_printf(mon, "%s: filename expected\n",
                                       cmdname);
                        break;
                    case 'B':
                        monitor_printf(mon, "%s: block device name expected\n",
                                       cmdname);
                        break;
                    default:
                        monitor_printf(mon, "%s: string expected\n", cmdname);
                        break;
                    }
                    goto fail;
                }
                str = qemu_malloc(strlen(buf) + 1);
                pstrcpy(str, sizeof(buf), buf);
                str_allocated[nb_args] = str;
            add_str:
                if (nb_args >= MAX_ARGS) {
                error_args:
                    monitor_printf(mon, "%s: too many arguments\n", cmdname);
                    goto fail;
                }
                args[nb_args++] = str;
            }
            break;
        case '/':
            {
                int count, format, size;

                while (qemu_isspace(*p))
                    p++;
                if (*p == '/') {
                    /* format found */
                    p++;
                    count = 1;
                    if (qemu_isdigit(*p)) {
                        count = 0;
                        while (qemu_isdigit(*p)) {
                            count = count * 10 + (*p - '0');
                            p++;
                        }
                    }
                    size = -1;
                    format = -1;
                    for(;;) {
                        switch(*p) {
                        case 'o':
                        case 'd':
                        case 'u':
                        case 'x':
                        case 'i':
                        case 'c':
                            format = *p++;
                            break;
                        case 'b':
                            size = 1;
                            p++;
                            break;
                        case 'h':
                            size = 2;
                            p++;
                            break;
                        case 'w':
                            size = 4;
                            p++;
                            break;
                        case 'g':
                        case 'L':
                            size = 8;
                            p++;
                            break;
                        default:
                            goto next;
                        }
                    }
                next:
                    if (*p != '\0' && !qemu_isspace(*p)) {
                        monitor_printf(mon, "invalid char in format: '%c'\n",
                                       *p);
                        goto fail;
                    }
                    if (format < 0)
                        format = default_fmt_format;
                    if (format != 'i') {
                        /* for 'i', not specifying a size gives -1 as size */
                        if (size < 0)
                            size = default_fmt_size;
                        default_fmt_size = size;
                    }
                    default_fmt_format = format;
                } else {
                    count = 1;
                    format = default_fmt_format;
                    if (format != 'i') {
                        size = default_fmt_size;
                    } else {
                        size = -1;
                    }
                }
                if (nb_args + 3 > MAX_ARGS)
                    goto error_args;
                args[nb_args++] = (void*)(long)count;
                args[nb_args++] = (void*)(long)format;
                args[nb_args++] = (void*)(long)size;
            }
            break;
        case 'i':
        case 'l':
            {
                int64_t val;

                while (qemu_isspace(*p))
                    p++;
                if (*typestr == '?' || *typestr == '.') {
                    if (*typestr == '?') {
                        if (*p == '\0')
                            has_arg = 0;
                        else
                            has_arg = 1;
                    } else {
                        if (*p == '.') {
                            p++;
                            while (qemu_isspace(*p))
                                p++;
                            has_arg = 1;
                        } else {
                            has_arg = 0;
                        }
                    }
                    typestr++;
                    if (nb_args >= MAX_ARGS)
                        goto error_args;
                    args[nb_args++] = (void *)(long)has_arg;
                    if (!has_arg) {
                        if (nb_args >= MAX_ARGS)
                            goto error_args;
                        val = -1;
                        goto add_num;
                    }
                }
                if (get_expr(mon, &val, &p))
                    goto fail;
            add_num:
                if (c == 'i') {
                    if (nb_args >= MAX_ARGS)
                        goto error_args;
                    args[nb_args++] = (void *)(long)val;
                } else {
                    if ((nb_args + 1) >= MAX_ARGS)
                        goto error_args;
#if TARGET_PHYS_ADDR_BITS > 32
                    args[nb_args++] = (void *)(long)((val >> 32) & 0xffffffff);
#else
                    args[nb_args++] = (void *)0;
#endif
                    args[nb_args++] = (void *)(long)(val & 0xffffffff);
                }
            }
            break;
        case '-':
            {
                int has_option;
                /* option */

                c = *typestr++;
                if (c == '\0')
                    goto bad_type;
                while (qemu_isspace(*p))
                    p++;
                has_option = 0;
                if (*p == '-') {
                    p++;
                    if (*p != c) {
                        monitor_printf(mon, "%s: unsupported option -%c\n",
                                       cmdname, *p);
                        goto fail;
                    }
                    p++;
                    has_option = 1;
                }
                if (nb_args >= MAX_ARGS)
                    goto error_args;
                args[nb_args++] = (void *)(long)has_option;
            }
            break;
        default:
        bad_type:
            monitor_printf(mon, "%s: unknown type '%c'\n", cmdname, c);
            goto fail;
        }
    }
    /* check that all arguments were parsed */
    while (qemu_isspace(*p))
        p++;
    if (*p != '\0') {
        monitor_printf(mon, "%s: extraneous characters at the end of line\n",
                       cmdname);
        goto fail;
    }

    switch(nb_args) {
    case 0:
        handler_0 = cmd->handler;
        handler_0(mon);
        break;
    case 1:
        handler_1 = cmd->handler;
        handler_1(mon, args[0]);
        break;
    case 2:
        handler_2 = cmd->handler;
        handler_2(mon, args[0], args[1]);
        break;
    case 3:
        handler_3 = cmd->handler;
        handler_3(mon, args[0], args[1], args[2]);
        break;
    case 4:
        handler_4 = cmd->handler;
        handler_4(mon, args[0], args[1], args[2], args[3]);
        break;
    case 5:
        handler_5 = cmd->handler;
        handler_5(mon, args[0], args[1], args[2], args[3], args[4]);
        break;
    case 6:
        handler_6 = cmd->handler;
        handler_6(mon, args[0], args[1], args[2], args[3], args[4], args[5]);
        break;
    case 7:
        handler_7 = cmd->handler;
        handler_7(mon, args[0], args[1], args[2], args[3], args[4], args[5],
                  args[6]);
        break;
    default:
        monitor_printf(mon, "unsupported number of arguments: %d\n", nb_args);
        goto fail;
    }
 fail:
    for(i = 0; i < MAX_ARGS; i++)
        qemu_free(str_allocated[i]);
    return;
}

static void cmd_completion(const char *name, const char *list)
{
    const char *p, *pstart;
    char cmd[128];
    int len;

    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        len = p - pstart;
        if (len > sizeof(cmd) - 2)
            len = sizeof(cmd) - 2;
        memcpy(cmd, pstart, len);
        cmd[len] = '\0';
        if (name[0] == '\0' || !strncmp(name, cmd, strlen(name))) {
            readline_add_completion(cur_mon->rs, cmd);
        }
        if (*p == '\0')
            break;
        p++;
    }
}

static void file_completion(const char *input)
{
    DIR *ffs;
    struct dirent *d;
    char path[1024];
    char file[1024], file_prefix[1024];
    int input_path_len;
    const char *p;

    p = strrchr(input, '/');
    if (!p) {
        input_path_len = 0;
        pstrcpy(file_prefix, sizeof(file_prefix), input);
        pstrcpy(path, sizeof(path), ".");
    } else {
        input_path_len = p - input + 1;
        memcpy(path, input, input_path_len);
        if (input_path_len > sizeof(path) - 1)
            input_path_len = sizeof(path) - 1;
        path[input_path_len] = '\0';
        pstrcpy(file_prefix, sizeof(file_prefix), p + 1);
    }
#ifdef DEBUG_COMPLETION
    monitor_printf(cur_mon, "input='%s' path='%s' prefix='%s'\n",
                   input, path, file_prefix);
#endif
    ffs = opendir(path);
    if (!ffs)
        return;
    for(;;) {
        struct stat sb;
        d = readdir(ffs);
        if (!d)
            break;
        if (strstart(d->d_name, file_prefix, NULL)) {
            memcpy(file, input, input_path_len);
            if (input_path_len < sizeof(file))
                pstrcpy(file + input_path_len, sizeof(file) - input_path_len,
                        d->d_name);
            /* stat the file to find out if it's a directory.
             * In that case add a slash to speed up typing long paths
             */
            stat(file, &sb);
            if(S_ISDIR(sb.st_mode))
                pstrcat(file, sizeof(file), "/");
            readline_add_completion(cur_mon->rs, file);
        }
    }
    closedir(ffs);
}

static void block_completion_it(void *opaque, BlockDriverState *bs)
{
    const char *name = bdrv_get_device_name(bs);
    const char *input = opaque;

    if (input[0] == '\0' ||
        !strncmp(name, (char *)input, strlen(input))) {
        readline_add_completion(cur_mon->rs, name);
    }
}

/* NOTE: this parser is an approximate form of the real command parser */
static void parse_cmdline(const char *cmdline,
                         int *pnb_args, char **args)
{
    const char *p;
    int nb_args, ret;
    char buf[1024];

    p = cmdline;
    nb_args = 0;
    for(;;) {
        while (qemu_isspace(*p))
            p++;
        if (*p == '\0')
            break;
        if (nb_args >= MAX_ARGS)
            break;
        ret = get_str(buf, sizeof(buf), &p);
        args[nb_args] = qemu_strdup(buf);
        nb_args++;
        if (ret < 0)
            break;
    }
    *pnb_args = nb_args;
}

static void monitor_find_completion(const char *cmdline)
{
    const char *cmdname;
    char *args[MAX_ARGS];
    int nb_args, i, len;
    const char *ptype, *str;
    const mon_cmd_t *cmd;
    const KeyDef *key;

    parse_cmdline(cmdline, &nb_args, args);
#ifdef DEBUG_COMPLETION
    for(i = 0; i < nb_args; i++) {
        monitor_printf(cur_mon, "arg%d = '%s'\n", i, (char *)args[i]);
    }
#endif

    /* if the line ends with a space, it means we want to complete the
       next arg */
    len = strlen(cmdline);
    if (len > 0 && qemu_isspace(cmdline[len - 1])) {
        if (nb_args >= MAX_ARGS)
            return;
        args[nb_args++] = qemu_strdup("");
    }
    if (nb_args <= 1) {
        /* command completion */
        if (nb_args == 0)
            cmdname = "";
        else
            cmdname = args[0];
        readline_set_completion_index(cur_mon->rs, strlen(cmdname));
        for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
            cmd_completion(cmdname, cmd->name);
        }
    } else {
        /* find the command */
        for(cmd = mon_cmds; cmd->name != NULL; cmd++) {
            if (compare_cmd(args[0], cmd->name))
                goto found;
        }
        return;
    found:
        ptype = cmd->args_type;
        for(i = 0; i < nb_args - 2; i++) {
            if (*ptype != '\0') {
                ptype++;
                while (*ptype == '?')
                    ptype++;
            }
        }
        str = args[nb_args - 1];
        switch(*ptype) {
        case 'F':
            /* file completion */
            readline_set_completion_index(cur_mon->rs, strlen(str));
            file_completion(str);
            break;
        case 'B':
            /* block device name completion */
            readline_set_completion_index(cur_mon->rs, strlen(str));
            bdrv_iterate(block_completion_it, (void *)str);
            break;
        case 's':
            /* XXX: more generic ? */
            if (!strcmp(cmd->name, "info")) {
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(cmd = info_cmds; cmd->name != NULL; cmd++) {
                    cmd_completion(str, cmd->name);
                }
            } else if (!strcmp(cmd->name, "sendkey")) {
                char *sep = strrchr(str, '-');
                if (sep)
                    str = sep + 1;
                readline_set_completion_index(cur_mon->rs, strlen(str));
                for(key = key_defs; key->name != NULL; key++) {
                    cmd_completion(str, key->name);
                }
            }
            break;
        default:
            break;
        }
    }
    for(i = 0; i < nb_args; i++)
        qemu_free(args[i]);
}

static int monitor_can_read(void *opaque)
{
    Monitor *mon = opaque;

    return (mon->suspend_cnt == 0) ? 128 : 0;
}

static void monitor_read(void *opaque, const uint8_t *buf, int size)
{
    Monitor *old_mon = cur_mon;
    int i;

    cur_mon = opaque;

    if (cur_mon->rs) {
        for (i = 0; i < size; i++)
            readline_handle_byte(cur_mon->rs, buf[i]);
    } else {
        if (size == 0 || buf[size - 1] != 0)
            monitor_printf(cur_mon, "corrupted command\n");
        else
            monitor_handle_command(cur_mon, (char *)buf);
    }

    cur_mon = old_mon;
}

static void monitor_command_cb(Monitor *mon, const char *cmdline, void *opaque)
{
    monitor_suspend(mon);
    monitor_handle_command(mon, cmdline);
    monitor_resume(mon);
}

int monitor_suspend(Monitor *mon)
{
    if (!mon->rs)
        return -ENOTTY;
    mon->suspend_cnt++;
    return 0;
}

void monitor_resume(Monitor *mon)
{
    if (!mon->rs)
        return;
    if (--mon->suspend_cnt == 0)
        readline_show_prompt(mon->rs);
}

static void monitor_event(void *opaque, int event)
{
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_MUX_IN:
        readline_restart(mon->rs);
        monitor_resume(mon);
        monitor_flush(mon);
        break;

    case CHR_EVENT_MUX_OUT:
        if (mon->suspend_cnt == 0)
            monitor_printf(mon, "\n");
        monitor_flush(mon);
        monitor_suspend(mon);
        break;

    case CHR_EVENT_RESET:
        monitor_printf(mon, "QEMU %s monitor - type 'help' for more "
                       "information\n", QEMU_VERSION);
        if (mon->chr->focus == 0)
            readline_show_prompt(mon->rs);
        break;
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */

void monitor_init(CharDriverState *chr, int flags)
{
    static int is_first_init = 1;
    Monitor *mon;

    if (is_first_init) {
        key_timer = qemu_new_timer(vm_clock, release_keys, NULL);
        is_first_init = 0;
    }

    mon = qemu_mallocz(sizeof(*mon));

    mon->chr = chr;
    mon->flags = flags;
    if (mon->chr->focus != 0)
        mon->suspend_cnt = 1; /* mux'ed monitors start suspended */
    if (flags & MONITOR_USE_READLINE) {
        mon->rs = readline_init(mon, monitor_find_completion);
        monitor_read_command(mon, 0);
    }

    qemu_chr_add_handlers(chr, monitor_can_read, monitor_read, monitor_event,
                          mon);

    LIST_INSERT_HEAD(&mon_list, mon, entry);
    if (!cur_mon || (flags & MONITOR_IS_DEFAULT))
        cur_mon = mon;
}

static void bdrv_password_cb(Monitor *mon, const char *password, void *opaque)
{
    BlockDriverState *bs = opaque;
    int ret = 0;

    if (bdrv_set_key(bs, password) != 0) {
        monitor_printf(mon, "invalid password\n");
        ret = -EPERM;
    }
    if (mon->password_completion_cb)
        mon->password_completion_cb(mon->password_opaque, ret);

    monitor_read_command(mon, 1);
}

void monitor_read_bdrv_key_start(Monitor *mon, BlockDriverState *bs,
                                 BlockDriverCompletionFunc *completion_cb,
                                 void *opaque)
{
    int err;

    if (!bdrv_key_required(bs)) {
        if (completion_cb)
            completion_cb(opaque, 0);
        return;
    }

    monitor_printf(mon, "%s (%s) is encrypted.\n", bdrv_get_device_name(bs),
                   bdrv_get_encrypted_filename(bs));

    mon->password_completion_cb = completion_cb;
    mon->password_opaque = opaque;

    err = monitor_read_password(mon, bdrv_password_cb, bs);

    if (err && completion_cb)
        completion_cb(opaque, err);
}
