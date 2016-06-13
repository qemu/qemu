/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu-version.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"

#ifdef CONFIG_SECCOMP
#include "sysemu/seccomp.h"
#endif

#if defined(CONFIG_VDE)
#include <libvdeplug.h>
#endif

#ifdef CONFIG_SDL
#if defined(__APPLE__) || defined(main)
#include <SDL.h>
int qemu_main(int argc, char **argv, char **envp);
int main(int argc, char **argv)
{
    return qemu_main(argc, argv, NULL);
}
#undef main
#define main qemu_main
#endif
#endif /* CONFIG_SDL */

#ifdef CONFIG_COCOA
#undef main
#define main qemu_main
#endif /* CONFIG_COCOA */


#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "sysemu/accel.h"
#include "hw/usb.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"
#include "hw/bt.h"
#include "sysemu/watchdog.h"
#include "hw/smbios/smbios.h"
#include "hw/xen/xen.h"
#include "hw/qdev.h"
#include "hw/loader.h"
#include "monitor/qdev.h"
#include "sysemu/bt.h"
#include "net/net.h"
#include "net/slirp.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "migration/block.h"
#include "sysemu/tpm.h"
#include "sysemu/dma.h"
#include "audio/audio.h"
#include "migration/migration.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "qapi/qmp/qjson.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu-options.h"
#include "qmp-commands.h"
#include "qemu/main-loop.h"
#ifdef CONFIG_VIRTFS
#include "fsdev/qemu-fsdev.h"
#endif
#include "sysemu/qtest.h"

#include "disas/disas.h"


#include "slirp/libslirp.h"

#include "trace.h"
#include "trace/control.h"
#include "qemu/queue.h"
#include "sysemu/cpus.h"
#include "sysemu/arch_init.h"

#include "ui/qemu-spice.h"
#include "qapi/string-input-visitor.h"
#include "qapi/opts-visitor.h"
#include "qom/object_interfaces.h"
#include "qapi-event.h"
#include "exec/semihost.h"
#include "crypto/init.h"
#include "sysemu/replay.h"
#include "qapi/qmp/qerror.h"

#define MAX_VIRTIO_CONSOLES 1
#define MAX_SCLP_CONSOLES 1

static const char *data_dir[16];
static int data_dir_idx;
const char *bios_name = NULL;
enum vga_retrace_method vga_retrace_method = VGA_RETRACE_DUMB;
int request_opengl = -1;
int display_opengl;
const char* keyboard_layout = NULL;
ram_addr_t ram_size;
const char *mem_path = NULL;
int mem_prealloc = 0; /* force preallocation of physical target memory */
bool enable_mlock = false;
int nb_nics;
NICInfo nd_table[MAX_NICS];
int autostart;
static int rtc_utc = 1;
static int rtc_date_offset = -1; /* -1 means no change */
QEMUClockType rtc_clock;
int vga_interface_type = VGA_NONE;
static int full_screen = 0;
static int no_frame = 0;
int no_quit = 0;
static bool grab_on_hover;
CharDriverState *serial_hds[MAX_SERIAL_PORTS];
CharDriverState *parallel_hds[MAX_PARALLEL_PORTS];
CharDriverState *virtcon_hds[MAX_VIRTIO_CONSOLES];
CharDriverState *sclp_hds[MAX_SCLP_CONSOLES];
int win2k_install_hack = 0;
int singlestep = 0;
int smp_cpus = 1;
int max_cpus = 0;
int smp_cores = 1;
int smp_threads = 1;
int acpi_enabled = 1;
int no_hpet = 0;
int fd_bootchk = 1;
static int no_reboot;
int no_shutdown = 0;
int cursor_hide = 1;
int graphic_rotate = 0;
const char *watchdog;
QEMUOptionRom option_rom[MAX_OPTION_ROMS];
int nb_option_roms;
int old_param = 0;
const char *qemu_name;
int alt_grab = 0;
int ctrl_grab = 0;
unsigned int nb_prom_envs = 0;
const char *prom_envs[MAX_PROM_ENVS];
int boot_menu;
bool boot_strict;
uint8_t *boot_splash_filedata;
size_t boot_splash_filedata_size;
uint8_t qemu_extra_params_fw[2];

int icount_align_option;

/* The bytes in qemu_uuid[] are in the order specified by RFC4122, _not_ in the
 * little-endian "wire format" described in the SMBIOS 2.6 specification.
 */
uint8_t qemu_uuid[16];
bool qemu_uuid_set;

static NotifierList exit_notifiers =
    NOTIFIER_LIST_INITIALIZER(exit_notifiers);

static NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);

bool xen_allowed;
uint32_t xen_domid;
enum xen_mode xen_mode = XEN_EMULATE;

static int has_defaults = 1;
static int default_serial = 1;
static int default_parallel = 1;
static int default_virtcon = 1;
static int default_sclp = 1;
static int default_monitor = 1;
static int default_floppy = 1;
static int default_cdrom = 1;
static int default_sdcard = 1;
static int default_vga = 1;
static int default_net = 1;

static struct {
    const char *driver;
    int *flag;
} default_list[] = {
    { .driver = "isa-serial",           .flag = &default_serial    },
    { .driver = "isa-parallel",         .flag = &default_parallel  },
    { .driver = "isa-fdc",              .flag = &default_floppy    },
    { .driver = "ide-cd",               .flag = &default_cdrom     },
    { .driver = "ide-hd",               .flag = &default_cdrom     },
    { .driver = "ide-drive",            .flag = &default_cdrom     },
    { .driver = "scsi-cd",              .flag = &default_cdrom     },
    { .driver = "virtio-serial-pci",    .flag = &default_virtcon   },
    { .driver = "virtio-serial",        .flag = &default_virtcon   },
    { .driver = "VGA",                  .flag = &default_vga       },
    { .driver = "isa-vga",              .flag = &default_vga       },
    { .driver = "cirrus-vga",           .flag = &default_vga       },
    { .driver = "isa-cirrus-vga",       .flag = &default_vga       },
    { .driver = "vmware-svga",          .flag = &default_vga       },
    { .driver = "qxl-vga",              .flag = &default_vga       },
    { .driver = "virtio-vga",           .flag = &default_vga       },
};

static QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .desc = {
        {
            .name = "base",
            .type = QEMU_OPT_STRING,
        },{
            .name = "clock",
            .type = QEMU_OPT_STRING,
        },{
            .name = "driftfix",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_sandbox_opts = {
    .name = "sandbox",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_sandbox_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_trace_opts = {
    .name = "trace",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_trace_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "events",
            .type = QEMU_OPT_STRING,
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_option_rom_opts = {
    .name = "option-rom",
    .implied_opt_name = "romfile",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_option_rom_opts.head),
    .desc = {
        {
            .name = "bootindex",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "romfile",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_machine_opts = {
    .name = "machine",
    .implied_opt_name = "type",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_machine_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting machine properties
         */
        { }
    },
};

static QemuOptsList qemu_boot_opts = {
    .name = "boot-opts",
    .implied_opt_name = "order",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_boot_opts.head),
    .desc = {
        {
            .name = "order",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "once",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "menu",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "splash",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "splash-time",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "reboot-timeout",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "strict",
            .type = QEMU_OPT_BOOL,
        },
        { /*End of list */ }
    },
};

static QemuOptsList qemu_add_fd_opts = {
    .name = "add-fd",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_add_fd_opts.head),
    .desc = {
        {
            .name = "fd",
            .type = QEMU_OPT_NUMBER,
            .help = "file descriptor of which a duplicate is added to fd set",
        },{
            .name = "set",
            .type = QEMU_OPT_NUMBER,
            .help = "ID of the fd set to add fd to",
        },{
            .name = "opaque",
            .type = QEMU_OPT_STRING,
            .help = "free-form string used to describe fd",
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};

static QemuOptsList qemu_tpmdev_opts = {
    .name = "tpmdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_tpmdev_opts.head),
    .desc = {
        /* options are defined in the TPM backends */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_realtime_opts = {
    .name = "realtime",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_realtime_opts.head),
    .desc = {
        {
            .name = "mlock",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_msg_opts = {
    .name = "msg",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_msg_opts.head),
    .desc = {
        {
            .name = "timestamp",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_name_opts = {
    .name = "name",
    .implied_opt_name = "guest",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_name_opts.head),
    .desc = {
        {
            .name = "guest",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the guest.\n"
                    "This name will be displayed in the SDL window caption.\n"
                    "The name will also be used for the VNC server",
        }, {
            .name = "process",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the QEMU process, as shown in top etc",
        }, {
            .name = "debug-threads",
            .type = QEMU_OPT_BOOL,
            .help = "When enabled, name the individual threads; defaults off.\n"
                    "NOTE: The thread names are for debugging and not a\n"
                    "stable API.",
        },
        { /* End of list */ }
    },
};

static QemuOptsList qemu_mem_opts = {
    .name = "memory",
    .implied_opt_name = "size",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mem_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },
        {
            .name = "slots",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "maxmem",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_icount_opts = {
    .name = "icount",
    .implied_opt_name = "shift",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_icount_opts.head),
    .desc = {
        {
            .name = "shift",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "align",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "sleep",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "rr",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "rrfile",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_semihosting_config_opts = {
    .name = "semihosting-config",
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_semihosting_config_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "target",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "arg",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_fw_cfg_opts = {
    .name = "fw_cfg",
    .implied_opt_name = "name",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_fw_cfg_opts.head),
    .desc = {
        {
            .name = "name",
            .type = QEMU_OPT_STRING,
            .help = "Sets the fw_cfg name of the blob to be inserted",
        }, {
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the file from which\n"
                    "the fw_cfg blob will be loaded",
        }, {
            .name = "string",
            .type = QEMU_OPT_STRING,
            .help = "Sets content of the blob to be inserted from a string",
        },
        { /* end of list */ }
    },
};

/**
 * Get machine options
 *
 * Returns: machine options (never null).
 */
QemuOpts *qemu_get_machine_opts(void)
{
    return qemu_find_opts_singleton("machine");
}

const char *qemu_get_vm_name(void)
{
    return qemu_name;
}

static void res_free(void)
{
    g_free(boot_splash_filedata);
    boot_splash_filedata = NULL;
}

static int default_driver_check(void *opaque, QemuOpts *opts, Error **errp)
{
    const char *driver = qemu_opt_get(opts, "driver");
    int i;

    if (!driver)
        return 0;
    for (i = 0; i < ARRAY_SIZE(default_list); i++) {
        if (strcmp(default_list[i].driver, driver) != 0)
            continue;
        *(default_list[i].flag) = 0;
    }
    return 0;
}

/***********************************************************/
/* QEMU state */

static RunState current_run_state = RUN_STATE_PRELAUNCH;

/* We use RUN_STATE__MAX but any invalid value will do */
static RunState vmstop_requested = RUN_STATE__MAX;
static QemuMutex vmstop_lock;

typedef struct {
    RunState from;
    RunState to;
} RunStateTransition;

static const RunStateTransition runstate_transitions_def[] = {
    /*     from      ->     to      */
    { RUN_STATE_DEBUG, RUN_STATE_RUNNING },
    { RUN_STATE_DEBUG, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_DEBUG, RUN_STATE_PRELAUNCH },

    { RUN_STATE_INMIGRATE, RUN_STATE_INTERNAL_ERROR },
    { RUN_STATE_INMIGRATE, RUN_STATE_IO_ERROR },
    { RUN_STATE_INMIGRATE, RUN_STATE_PAUSED },
    { RUN_STATE_INMIGRATE, RUN_STATE_RUNNING },
    { RUN_STATE_INMIGRATE, RUN_STATE_SHUTDOWN },
    { RUN_STATE_INMIGRATE, RUN_STATE_SUSPENDED },
    { RUN_STATE_INMIGRATE, RUN_STATE_WATCHDOG },
    { RUN_STATE_INMIGRATE, RUN_STATE_GUEST_PANICKED },
    { RUN_STATE_INMIGRATE, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_INMIGRATE, RUN_STATE_PRELAUNCH },
    { RUN_STATE_INMIGRATE, RUN_STATE_POSTMIGRATE },

    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_PAUSED },
    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_PRELAUNCH },

    { RUN_STATE_IO_ERROR, RUN_STATE_RUNNING },
    { RUN_STATE_IO_ERROR, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_IO_ERROR, RUN_STATE_PRELAUNCH },

    { RUN_STATE_PAUSED, RUN_STATE_RUNNING },
    { RUN_STATE_PAUSED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_PAUSED, RUN_STATE_PRELAUNCH },

    { RUN_STATE_POSTMIGRATE, RUN_STATE_RUNNING },
    { RUN_STATE_POSTMIGRATE, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_POSTMIGRATE, RUN_STATE_PRELAUNCH },

    { RUN_STATE_PRELAUNCH, RUN_STATE_RUNNING },
    { RUN_STATE_PRELAUNCH, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_PRELAUNCH, RUN_STATE_INMIGRATE },

    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_RUNNING },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_POSTMIGRATE },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_PRELAUNCH },

    { RUN_STATE_RESTORE_VM, RUN_STATE_RUNNING },
    { RUN_STATE_RESTORE_VM, RUN_STATE_PRELAUNCH },

    { RUN_STATE_RUNNING, RUN_STATE_DEBUG },
    { RUN_STATE_RUNNING, RUN_STATE_INTERNAL_ERROR },
    { RUN_STATE_RUNNING, RUN_STATE_IO_ERROR },
    { RUN_STATE_RUNNING, RUN_STATE_PAUSED },
    { RUN_STATE_RUNNING, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_RUNNING, RUN_STATE_RESTORE_VM },
    { RUN_STATE_RUNNING, RUN_STATE_SAVE_VM },
    { RUN_STATE_RUNNING, RUN_STATE_SHUTDOWN },
    { RUN_STATE_RUNNING, RUN_STATE_WATCHDOG },
    { RUN_STATE_RUNNING, RUN_STATE_GUEST_PANICKED },

    { RUN_STATE_SAVE_VM, RUN_STATE_RUNNING },

    { RUN_STATE_SHUTDOWN, RUN_STATE_PAUSED },
    { RUN_STATE_SHUTDOWN, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SHUTDOWN, RUN_STATE_PRELAUNCH },

    { RUN_STATE_DEBUG, RUN_STATE_SUSPENDED },
    { RUN_STATE_RUNNING, RUN_STATE_SUSPENDED },
    { RUN_STATE_SUSPENDED, RUN_STATE_RUNNING },
    { RUN_STATE_SUSPENDED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SUSPENDED, RUN_STATE_PRELAUNCH },

    { RUN_STATE_WATCHDOG, RUN_STATE_RUNNING },
    { RUN_STATE_WATCHDOG, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_WATCHDOG, RUN_STATE_PRELAUNCH },

    { RUN_STATE_GUEST_PANICKED, RUN_STATE_RUNNING },
    { RUN_STATE_GUEST_PANICKED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_GUEST_PANICKED, RUN_STATE_PRELAUNCH },

    { RUN_STATE__MAX, RUN_STATE__MAX },
};

static bool runstate_valid_transitions[RUN_STATE__MAX][RUN_STATE__MAX];

bool runstate_check(RunState state)
{
    return current_run_state == state;
}

bool runstate_store(char *str, size_t size)
{
    const char *state = RunState_lookup[current_run_state];
    size_t len = strlen(state) + 1;

    if (len > size) {
        return false;
    }
    memcpy(str, state, len);
    return true;
}

static void runstate_init(void)
{
    const RunStateTransition *p;

    memset(&runstate_valid_transitions, 0, sizeof(runstate_valid_transitions));
    for (p = &runstate_transitions_def[0]; p->from != RUN_STATE__MAX; p++) {
        runstate_valid_transitions[p->from][p->to] = true;
    }

    qemu_mutex_init(&vmstop_lock);
}

/* This function will abort() on invalid state transitions */
void runstate_set(RunState new_state)
{
    assert(new_state < RUN_STATE__MAX);

    if (current_run_state == new_state) {
        return;
    }

    if (!runstate_valid_transitions[current_run_state][new_state]) {
        error_report("invalid runstate transition: '%s' -> '%s'",
                     RunState_lookup[current_run_state],
                     RunState_lookup[new_state]);
        abort();
    }
    trace_runstate_set(new_state);
    current_run_state = new_state;
}

int runstate_is_running(void)
{
    return runstate_check(RUN_STATE_RUNNING);
}

bool runstate_needs_reset(void)
{
    return runstate_check(RUN_STATE_INTERNAL_ERROR) ||
        runstate_check(RUN_STATE_SHUTDOWN);
}

StatusInfo *qmp_query_status(Error **errp)
{
    StatusInfo *info = g_malloc0(sizeof(*info));

    info->running = runstate_is_running();
    info->singlestep = singlestep;
    info->status = current_run_state;

    return info;
}

static bool qemu_vmstop_requested(RunState *r)
{
    qemu_mutex_lock(&vmstop_lock);
    *r = vmstop_requested;
    vmstop_requested = RUN_STATE__MAX;
    qemu_mutex_unlock(&vmstop_lock);
    return *r < RUN_STATE__MAX;
}

void qemu_system_vmstop_request_prepare(void)
{
    qemu_mutex_lock(&vmstop_lock);
}

void qemu_system_vmstop_request(RunState state)
{
    vmstop_requested = state;
    qemu_mutex_unlock(&vmstop_lock);
    qemu_notify_event();
}

void vm_start(void)
{
    RunState requested;

    qemu_vmstop_requested(&requested);
    if (runstate_is_running() && requested == RUN_STATE__MAX) {
        return;
    }

    /* Ensure that a STOP/RESUME pair of events is emitted if a
     * vmstop request was pending.  The BLOCK_IO_ERROR event, for
     * example, according to documentation is always followed by
     * the STOP event.
     */
    if (runstate_is_running()) {
        qapi_event_send_stop(&error_abort);
    } else {
        cpu_enable_ticks();
        runstate_set(RUN_STATE_RUNNING);
        vm_state_notify(1, RUN_STATE_RUNNING);
        resume_all_vcpus();
    }

    qapi_event_send_resume(&error_abort);
}


/***********************************************************/
/* real time host monotonic timer */

static time_t qemu_time(void)
{
    return qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
}

/***********************************************************/
/* host time/date access */
void qemu_get_timedate(struct tm *tm, int offset)
{
    time_t ti = qemu_time();

    ti += offset;
    if (rtc_date_offset == -1) {
        if (rtc_utc)
            gmtime_r(&ti, tm);
        else
            localtime_r(&ti, tm);
    } else {
        ti -= rtc_date_offset;
        gmtime_r(&ti, tm);
    }
}

int qemu_timedate_diff(struct tm *tm)
{
    time_t seconds;

    if (rtc_date_offset == -1)
        if (rtc_utc)
            seconds = mktimegm(tm);
        else {
            struct tm tmp = *tm;
            tmp.tm_isdst = -1; /* use timezone to figure it out */
            seconds = mktime(&tmp);
	}
    else
        seconds = mktimegm(tm) + rtc_date_offset;

    return seconds - qemu_time();
}

static void configure_rtc_date_offset(const char *startdate, int legacy)
{
    time_t rtc_start_date;
    struct tm tm;

    if (!strcmp(startdate, "now") && legacy) {
        rtc_date_offset = -1;
    } else {
        if (sscanf(startdate, "%d-%d-%dT%d:%d:%d",
                   &tm.tm_year,
                   &tm.tm_mon,
                   &tm.tm_mday,
                   &tm.tm_hour,
                   &tm.tm_min,
                   &tm.tm_sec) == 6) {
            /* OK */
        } else if (sscanf(startdate, "%d-%d-%d",
                          &tm.tm_year,
                          &tm.tm_mon,
                          &tm.tm_mday) == 3) {
            tm.tm_hour = 0;
            tm.tm_min = 0;
            tm.tm_sec = 0;
        } else {
            goto date_fail;
        }
        tm.tm_year -= 1900;
        tm.tm_mon--;
        rtc_start_date = mktimegm(&tm);
        if (rtc_start_date == -1) {
        date_fail:
            error_report("invalid date format");
            error_printf("valid formats: "
                         "'2006-06-17T16:01:21' or '2006-06-17'\n");
            exit(1);
        }
        rtc_date_offset = qemu_time() - rtc_start_date;
    }
}

static void configure_rtc(QemuOpts *opts)
{
    const char *value;

    value = qemu_opt_get(opts, "base");
    if (value) {
        if (!strcmp(value, "utc")) {
            rtc_utc = 1;
        } else if (!strcmp(value, "localtime")) {
            Error *blocker = NULL;
            rtc_utc = 0;
            error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED,
                      "-rtc base=localtime");
            replay_add_blocker(blocker);
        } else {
            configure_rtc_date_offset(value, 0);
        }
    }
    value = qemu_opt_get(opts, "clock");
    if (value) {
        if (!strcmp(value, "host")) {
            rtc_clock = QEMU_CLOCK_HOST;
        } else if (!strcmp(value, "rt")) {
            rtc_clock = QEMU_CLOCK_REALTIME;
        } else if (!strcmp(value, "vm")) {
            rtc_clock = QEMU_CLOCK_VIRTUAL;
        } else {
            error_report("invalid option value '%s'", value);
            exit(1);
        }
    }
    value = qemu_opt_get(opts, "driftfix");
    if (value) {
        if (!strcmp(value, "slew")) {
            static GlobalProperty slew_lost_ticks = {
                .driver   = "mc146818rtc",
                .property = "lost_tick_policy",
                .value    = "slew",
            };

            qdev_prop_register_global(&slew_lost_ticks);
        } else if (!strcmp(value, "none")) {
            /* discard is default */
        } else {
            error_report("invalid option value '%s'", value);
            exit(1);
        }
    }
}

/***********************************************************/
/* Bluetooth support */
static int nb_hcis;
static int cur_hci;
static struct HCIInfo *hci_table[MAX_NICS];

struct HCIInfo *qemu_next_hci(void)
{
    if (cur_hci == nb_hcis)
        return &null_hci;

    return hci_table[cur_hci++];
}

static int bt_hci_parse(const char *str)
{
    struct HCIInfo *hci;
    bdaddr_t bdaddr;

    if (nb_hcis >= MAX_NICS) {
        error_report("too many bluetooth HCIs (max %i)", MAX_NICS);
        return -1;
    }

    hci = hci_init(str);
    if (!hci)
        return -1;

    bdaddr.b[0] = 0x52;
    bdaddr.b[1] = 0x54;
    bdaddr.b[2] = 0x00;
    bdaddr.b[3] = 0x12;
    bdaddr.b[4] = 0x34;
    bdaddr.b[5] = 0x56 + nb_hcis;
    hci->bdaddr_set(hci, bdaddr.b);

    hci_table[nb_hcis++] = hci;

    return 0;
}

static void bt_vhci_add(int vlan_id)
{
    struct bt_scatternet_s *vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave)
        error_report("warning: adding a VHCI to an empty scatternet %i",
                     vlan_id);

    bt_vhci_init(bt_new_hci(vlan));
}

static struct bt_device_s *bt_device_add(const char *opt)
{
    struct bt_scatternet_s *vlan;
    int vlan_id = 0;
    char *endp = strstr(opt, ",vlan=");
    int len = (endp ? endp - opt : strlen(opt)) + 1;
    char devname[10];

    pstrcpy(devname, MIN(sizeof(devname), len), opt);

    if (endp) {
        vlan_id = strtol(endp + 6, &endp, 0);
        if (*endp) {
            error_report("unrecognised bluetooth vlan Id");
            return 0;
        }
    }

    vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave)
        error_report("warning: adding a slave device to an empty scatternet %i",
                     vlan_id);

    if (!strcmp(devname, "keyboard"))
        return bt_keyboard_init(vlan);

    error_report("unsupported bluetooth device '%s'", devname);
    return 0;
}

static int bt_parse(const char *opt)
{
    const char *endp, *p;
    int vlan;

    if (strstart(opt, "hci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp)
                if (!strstart(endp, ",vlan=", 0))
                    opt = endp + 1;

            return bt_hci_parse(opt);
       }
    } else if (strstart(opt, "vhci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp) {
                if (strstart(endp, ",vlan=", &p)) {
                    vlan = strtol(p, (char **) &endp, 0);
                    if (*endp) {
                        error_report("bad scatternet '%s'", p);
                        return 1;
                    }
                } else {
                    error_report("bad parameter '%s'", endp + 1);
                    return 1;
                }
            } else
                vlan = 0;

            bt_vhci_add(vlan);
            return 0;
        }
    } else if (strstart(opt, "device:", &endp))
        return !bt_device_add(endp);

    error_report("bad bluetooth parameter '%s'", opt);
    return 1;
}

static int parse_sandbox(void *opaque, QemuOpts *opts, Error **errp)
{
    /* FIXME: change this to true for 1.3 */
    if (qemu_opt_get_bool(opts, "enable", false)) {
#ifdef CONFIG_SECCOMP
        if (seccomp_start() < 0) {
            error_report("failed to install seccomp syscall filter "
                         "in the kernel");
            return -1;
        }
#else
        error_report("seccomp support is disabled");
        return -1;
#endif
    }

    return 0;
}

static int parse_name(void *opaque, QemuOpts *opts, Error **errp)
{
    const char *proc_name;

    if (qemu_opt_get(opts, "debug-threads")) {
        qemu_thread_naming(qemu_opt_get_bool(opts, "debug-threads", false));
    }
    qemu_name = qemu_opt_get(opts, "guest");

    proc_name = qemu_opt_get(opts, "process");
    if (proc_name) {
        os_set_proc_name(proc_name);
    }

    return 0;
}

bool defaults_enabled(void)
{
    return has_defaults;
}

#ifndef _WIN32
static int parse_add_fd(void *opaque, QemuOpts *opts, Error **errp)
{
    int fd, dupfd, flags;
    int64_t fdset_id;
    const char *fd_opaque = NULL;
    AddfdInfo *fdinfo;

    fd = qemu_opt_get_number(opts, "fd", -1);
    fdset_id = qemu_opt_get_number(opts, "set", -1);
    fd_opaque = qemu_opt_get(opts, "opaque");

    if (fd < 0) {
        error_report("fd option is required and must be non-negative");
        return -1;
    }

    if (fd <= STDERR_FILENO) {
        error_report("fd cannot be a standard I/O stream");
        return -1;
    }

    /*
     * All fds inherited across exec() necessarily have FD_CLOEXEC
     * clear, while qemu sets FD_CLOEXEC on all other fds used internally.
     */
    flags = fcntl(fd, F_GETFD);
    if (flags == -1 || (flags & FD_CLOEXEC)) {
        error_report("fd is not valid or already in use");
        return -1;
    }

    if (fdset_id < 0) {
        error_report("set option is required and must be non-negative");
        return -1;
    }

#ifdef F_DUPFD_CLOEXEC
    dupfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
    dupfd = dup(fd);
    if (dupfd != -1) {
        qemu_set_cloexec(dupfd);
    }
#endif
    if (dupfd == -1) {
        error_report("error duplicating fd: %s", strerror(errno));
        return -1;
    }

    /* add the duplicate fd, and optionally the opaque string, to the fd set */
    fdinfo = monitor_fdset_add_fd(dupfd, true, fdset_id, !!fd_opaque, fd_opaque,
                                  &error_abort);
    g_free(fdinfo);

    return 0;
}

static int cleanup_add_fd(void *opaque, QemuOpts *opts, Error **errp)
{
    int fd;

    fd = qemu_opt_get_number(opts, "fd", -1);
    close(fd);

    return 0;
}
#endif

/***********************************************************/
/* QEMU Block devices */

#define HD_OPTS "media=disk"
#define CDROM_OPTS "media=cdrom"
#define FD_OPTS ""
#define PFLASH_OPTS ""
#define MTD_OPTS ""
#define SD_OPTS ""

static int drive_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    BlockInterfaceType *block_default_type = opaque;

    return drive_new(opts, *block_default_type) == NULL;
}

static int drive_enable_snapshot(void *opaque, QemuOpts *opts, Error **errp)
{
    if (qemu_opt_get(opts, "snapshot") == NULL) {
        qemu_opt_set(opts, "snapshot", "on", &error_abort);
    }
    return 0;
}

static void default_drive(int enable, int snapshot, BlockInterfaceType type,
                          int index, const char *optstr)
{
    QemuOpts *opts;
    DriveInfo *dinfo;

    if (!enable || drive_get_by_index(type, index)) {
        return;
    }

    opts = drive_add(type, index, NULL, optstr);
    if (snapshot) {
        drive_enable_snapshot(NULL, opts, NULL);
    }

    dinfo = drive_new(opts, type);
    if (!dinfo) {
        exit(1);
    }
    dinfo->is_default = true;

}

static QemuOptsList qemu_smp_opts = {
    .name = "smp-opts",
    .implied_opt_name = "cpus",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_smp_opts.head),
    .desc = {
        {
            .name = "cpus",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "sockets",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "cores",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "threads",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "maxcpus",
            .type = QEMU_OPT_NUMBER,
        },
        { /*End of list */ }
    },
};

static void smp_parse(QemuOpts *opts)
{
    if (opts) {

        unsigned cpus    = qemu_opt_get_number(opts, "cpus", 0);
        unsigned sockets = qemu_opt_get_number(opts, "sockets", 0);
        unsigned cores   = qemu_opt_get_number(opts, "cores", 0);
        unsigned threads = qemu_opt_get_number(opts, "threads", 0);

        /* compute missing values, prefer sockets over cores over threads */
        if (cpus == 0 || sockets == 0) {
            sockets = sockets > 0 ? sockets : 1;
            cores = cores > 0 ? cores : 1;
            threads = threads > 0 ? threads : 1;
            if (cpus == 0) {
                cpus = cores * threads * sockets;
            }
        } else if (cores == 0) {
            threads = threads > 0 ? threads : 1;
            cores = cpus / (sockets * threads);
        } else if (threads == 0) {
            threads = cpus / (cores * sockets);
        } else if (sockets * cores * threads < cpus) {
            error_report("cpu topology: "
                         "sockets (%u) * cores (%u) * threads (%u) < "
                         "smp_cpus (%u)",
                         sockets, cores, threads, cpus);
            exit(1);
        }

        max_cpus = qemu_opt_get_number(opts, "maxcpus", cpus);
        if (sockets * cores * threads > max_cpus) {
            error_report("cpu topology: "
                         "sockets (%u) * cores (%u) * threads (%u) > "
                         "maxcpus (%u)",
                         sockets, cores, threads, max_cpus);
            exit(1);
        }

        smp_cpus = cpus;
        smp_cores = cores > 0 ? cores : 1;
        smp_threads = threads > 0 ? threads : 1;

    }

    if (max_cpus == 0) {
        max_cpus = smp_cpus;
    }

    if (max_cpus > MAX_CPUMASK_BITS) {
        error_report("unsupported number of maxcpus");
        exit(1);
    }
    if (max_cpus < smp_cpus) {
        error_report("maxcpus must be equal to or greater than smp");
        exit(1);
    }

    if (smp_cpus > 1 || smp_cores > 1 || smp_threads > 1) {
        Error *blocker = NULL;
        error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED, "smp");
        replay_add_blocker(blocker);
    }
}

static void realtime_init(void)
{
    if (enable_mlock) {
        if (os_mlock() < 0) {
            error_report("locking memory failed");
            exit(1);
        }
    }
}


static void configure_msg(QemuOpts *opts)
{
    enable_timestamp_msg = qemu_opt_get_bool(opts, "timestamp", true);
}

/***********************************************************/
/* Semihosting */

typedef struct SemihostingConfig {
    bool enabled;
    SemihostingTarget target;
    const char **argv;
    int argc;
    const char *cmdline; /* concatenated argv */
} SemihostingConfig;

static SemihostingConfig semihosting;

bool semihosting_enabled(void)
{
    return semihosting.enabled;
}

SemihostingTarget semihosting_get_target(void)
{
    return semihosting.target;
}

const char *semihosting_get_arg(int i)
{
    if (i >= semihosting.argc) {
        return NULL;
    }
    return semihosting.argv[i];
}

int semihosting_get_argc(void)
{
    return semihosting.argc;
}

const char *semihosting_get_cmdline(void)
{
    if (semihosting.cmdline == NULL && semihosting.argc > 0) {
        semihosting.cmdline = g_strjoinv(" ", (gchar **)semihosting.argv);
    }
    return semihosting.cmdline;
}

static int add_semihosting_arg(void *opaque,
                               const char *name, const char *val,
                               Error **errp)
{
    SemihostingConfig *s = opaque;
    if (strcmp(name, "arg") == 0) {
        s->argc++;
        /* one extra element as g_strjoinv() expects NULL-terminated array */
        s->argv = g_realloc(s->argv, (s->argc + 1) * sizeof(void *));
        s->argv[s->argc - 1] = val;
        s->argv[s->argc] = NULL;
    }
    return 0;
}

/* Use strings passed via -kernel/-append to initialize semihosting.argv[] */
static inline void semihosting_arg_fallback(const char *file, const char *cmd)
{
    char *cmd_token;

    /* argv[0] */
    add_semihosting_arg(&semihosting, "arg", file, NULL);

    /* split -append and initialize argv[1..n] */
    cmd_token = strtok(g_strdup(cmd), " ");
    while (cmd_token) {
        add_semihosting_arg(&semihosting, "arg", cmd_token, NULL);
        cmd_token = strtok(NULL, " ");
    }
}

/* Now we still need this for compatibility with XEN. */
bool has_igd_gfx_passthru;
static void igd_gfx_passthru(void)
{
    has_igd_gfx_passthru = current_machine->igd_gfx_passthru;
}

/***********************************************************/
/* USB devices */

static int usb_device_add(const char *devname)
{
    USBDevice *dev = NULL;
#ifndef CONFIG_LINUX
    const char *p;
#endif

    if (!machine_usb(current_machine)) {
        return -1;
    }

    /* drivers with .usbdevice_name entry in USBDeviceInfo */
    dev = usbdevice_create(devname);
    if (dev)
        goto done;

    /* the other ones */
#ifndef CONFIG_LINUX
    /* only the linux version is qdev-ified, usb-bsd still needs this */
    if (strstart(devname, "host:", &p)) {
        dev = usb_host_device_open(usb_bus_find(-1), p);
    }
#endif
    if (!dev)
        return -1;

done:
    return 0;
}

static int usb_device_del(const char *devname)
{
    int bus_num, addr;
    const char *p;

    if (strstart(devname, "host:", &p)) {
        return -1;
    }

    if (!machine_usb(current_machine)) {
        return -1;
    }

    p = strchr(devname, '.');
    if (!p)
        return -1;
    bus_num = strtoul(devname, NULL, 0);
    addr = strtoul(p + 1, NULL, 0);

    return usb_device_delete_addr(bus_num, addr);
}

static int usb_parse(const char *cmdline)
{
    int r;
    r = usb_device_add(cmdline);
    if (r < 0) {
        error_report("could not add USB device '%s'", cmdline);
    }
    return r;
}

void hmp_usb_add(Monitor *mon, const QDict *qdict)
{
    const char *devname = qdict_get_str(qdict, "devname");
    if (usb_device_add(devname) < 0) {
        error_report("could not add USB device '%s'", devname);
    }
}

void hmp_usb_del(Monitor *mon, const QDict *qdict)
{
    const char *devname = qdict_get_str(qdict, "devname");
    if (usb_device_del(devname) < 0) {
        error_report("could not delete USB device '%s'", devname);
    }
}

/***********************************************************/
/* machine registration */

MachineState *current_machine;

static MachineClass *find_machine(const char *name)
{
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineClass *mc = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *temp = el->data;

        if (!strcmp(temp->name, name)) {
            mc = temp;
            break;
        }
        if (temp->alias &&
            !strcmp(temp->alias, name)) {
            mc = temp;
            break;
        }
    }

    g_slist_free(machines);
    return mc;
}

MachineClass *find_default_machine(void)
{
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineClass *mc = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *temp = el->data;

        if (temp->is_default) {
            mc = temp;
            break;
        }
    }

    g_slist_free(machines);
    return mc;
}

MachineInfoList *qmp_query_machines(Error **errp)
{
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineInfoList *mach_list = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;
        MachineInfoList *entry;
        MachineInfo *info;

        info = g_malloc0(sizeof(*info));
        if (mc->is_default) {
            info->has_is_default = true;
            info->is_default = true;
        }

        if (mc->alias) {
            info->has_alias = true;
            info->alias = g_strdup(mc->alias);
        }

        info->name = g_strdup(mc->name);
        info->cpu_max = !mc->max_cpus ? 1 : mc->max_cpus;

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = mach_list;
        mach_list = entry;
    }

    g_slist_free(machines);
    return mach_list;
}

static int machine_help_func(QemuOpts *opts, MachineState *machine)
{
    ObjectProperty *prop;
    ObjectPropertyIterator iter;

    if (!qemu_opt_has_help_opt(opts)) {
        return 0;
    }

    object_property_iter_init(&iter, OBJECT(machine));
    while ((prop = object_property_iter_next(&iter))) {
        if (!prop->set) {
            continue;
        }

        error_printf("%s.%s=%s", MACHINE_GET_CLASS(machine)->name,
                     prop->name, prop->type);
        if (prop->description) {
            error_printf(" (%s)\n", prop->description);
        } else {
            error_printf("\n");
        }
    }

    return 1;
}

/***********************************************************/
/* main execution loop */

struct vm_change_state_entry {
    VMChangeStateHandler *cb;
    void *opaque;
    QLIST_ENTRY (vm_change_state_entry) entries;
};

static QLIST_HEAD(vm_change_state_head, vm_change_state_entry) vm_change_state_head;

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    VMChangeStateEntry *e;

    e = g_malloc0(sizeof (*e));

    e->cb = cb;
    e->opaque = opaque;
    QLIST_INSERT_HEAD(&vm_change_state_head, e, entries);
    return e;
}

void qemu_del_vm_change_state_handler(VMChangeStateEntry *e)
{
    QLIST_REMOVE (e, entries);
    g_free (e);
}

void vm_state_notify(int running, RunState state)
{
    VMChangeStateEntry *e, *next;

    trace_vm_state_notify(running, state);

    QLIST_FOREACH_SAFE(e, &vm_change_state_head, entries, next) {
        e->cb(e->opaque, running, state);
    }
}

/* reset/shutdown handler */

typedef struct QEMUResetEntry {
    QTAILQ_ENTRY(QEMUResetEntry) entry;
    QEMUResetHandler *func;
    void *opaque;
} QEMUResetEntry;

static QTAILQ_HEAD(reset_handlers, QEMUResetEntry) reset_handlers =
    QTAILQ_HEAD_INITIALIZER(reset_handlers);
static int reset_requested;
static int shutdown_requested, shutdown_signal = -1;
static pid_t shutdown_pid;
static int powerdown_requested;
static int debug_requested;
static int suspend_requested;
static WakeupReason wakeup_reason;
static NotifierList powerdown_notifiers =
    NOTIFIER_LIST_INITIALIZER(powerdown_notifiers);
static NotifierList suspend_notifiers =
    NOTIFIER_LIST_INITIALIZER(suspend_notifiers);
static NotifierList wakeup_notifiers =
    NOTIFIER_LIST_INITIALIZER(wakeup_notifiers);
static uint32_t wakeup_reason_mask = ~(1 << QEMU_WAKEUP_REASON_NONE);

int qemu_shutdown_requested_get(void)
{
    return shutdown_requested;
}

int qemu_reset_requested_get(void)
{
    return reset_requested;
}

static int qemu_shutdown_requested(void)
{
    return atomic_xchg(&shutdown_requested, 0);
}

static void qemu_kill_report(void)
{
    if (!qtest_driver() && shutdown_signal != -1) {
        if (shutdown_pid == 0) {
            /* This happens for eg ^C at the terminal, so it's worth
             * avoiding printing an odd message in that case.
             */
            error_report("terminating on signal %d", shutdown_signal);
        } else {
            error_report("terminating on signal %d from pid " FMT_pid,
                         shutdown_signal, shutdown_pid);
        }
        shutdown_signal = -1;
    }
}

static int qemu_reset_requested(void)
{
    int r = reset_requested;
    if (r && replay_checkpoint(CHECKPOINT_RESET_REQUESTED)) {
        reset_requested = 0;
        return r;
    }
    return false;
}

static int qemu_suspend_requested(void)
{
    int r = suspend_requested;
    if (r && replay_checkpoint(CHECKPOINT_SUSPEND_REQUESTED)) {
        suspend_requested = 0;
        return r;
    }
    return false;
}

static WakeupReason qemu_wakeup_requested(void)
{
    return wakeup_reason;
}

static int qemu_powerdown_requested(void)
{
    int r = powerdown_requested;
    powerdown_requested = 0;
    return r;
}

static int qemu_debug_requested(void)
{
    int r = debug_requested;
    debug_requested = 0;
    return r;
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    QEMUResetEntry *re = g_malloc0(sizeof(QEMUResetEntry));

    re->func = func;
    re->opaque = opaque;
    QTAILQ_INSERT_TAIL(&reset_handlers, re, entry);
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
    QEMUResetEntry *re;

    QTAILQ_FOREACH(re, &reset_handlers, entry) {
        if (re->func == func && re->opaque == opaque) {
            QTAILQ_REMOVE(&reset_handlers, re, entry);
            g_free(re);
            return;
        }
    }
}

void qemu_devices_reset(void)
{
    QEMUResetEntry *re, *nre;

    /* reset all devices */
    QTAILQ_FOREACH_SAFE(re, &reset_handlers, entry, nre) {
        re->func(re->opaque);
    }
}

void qemu_system_reset(bool report)
{
    MachineClass *mc;

    mc = current_machine ? MACHINE_GET_CLASS(current_machine) : NULL;

    cpu_synchronize_all_states();

    if (mc && mc->reset) {
        mc->reset();
    } else {
        qemu_devices_reset();
    }
    if (report) {
        qapi_event_send_reset(&error_abort);
    }
    cpu_synchronize_all_post_reset();
}

void qemu_system_guest_panicked(void)
{
    if (current_cpu) {
        current_cpu->crash_occurred = true;
    }
    qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_PAUSE, &error_abort);
    vm_stop(RUN_STATE_GUEST_PANICKED);
}

void qemu_system_reset_request(void)
{
    if (no_reboot) {
        shutdown_requested = 1;
    } else {
        reset_requested = 1;
    }
    cpu_stop_current();
    qemu_notify_event();
}

static void qemu_system_suspend(void)
{
    pause_all_vcpus();
    notifier_list_notify(&suspend_notifiers, NULL);
    runstate_set(RUN_STATE_SUSPENDED);
    qapi_event_send_suspend(&error_abort);
}

void qemu_system_suspend_request(void)
{
    if (runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }
    suspend_requested = 1;
    cpu_stop_current();
    qemu_notify_event();
}

void qemu_register_suspend_notifier(Notifier *notifier)
{
    notifier_list_add(&suspend_notifiers, notifier);
}

void qemu_system_wakeup_request(WakeupReason reason)
{
    trace_system_wakeup_request(reason);

    if (!runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }
    if (!(wakeup_reason_mask & (1 << reason))) {
        return;
    }
    runstate_set(RUN_STATE_RUNNING);
    wakeup_reason = reason;
    qemu_notify_event();
}

void qemu_system_wakeup_enable(WakeupReason reason, bool enabled)
{
    if (enabled) {
        wakeup_reason_mask |= (1 << reason);
    } else {
        wakeup_reason_mask &= ~(1 << reason);
    }
}

void qemu_register_wakeup_notifier(Notifier *notifier)
{
    notifier_list_add(&wakeup_notifiers, notifier);
}

void qemu_system_killed(int signal, pid_t pid)
{
    shutdown_signal = signal;
    shutdown_pid = pid;
    no_shutdown = 0;

    /* Cannot call qemu_system_shutdown_request directly because
     * we are in a signal handler.
     */
    shutdown_requested = 1;
    qemu_notify_event();
}

void qemu_system_shutdown_request(void)
{
    trace_qemu_system_shutdown_request();
    replay_shutdown_request();
    shutdown_requested = 1;
    qemu_notify_event();
}

static void qemu_system_powerdown(void)
{
    qapi_event_send_powerdown(&error_abort);
    notifier_list_notify(&powerdown_notifiers, NULL);
}

void qemu_system_powerdown_request(void)
{
    trace_qemu_system_powerdown_request();
    powerdown_requested = 1;
    qemu_notify_event();
}

void qemu_register_powerdown_notifier(Notifier *notifier)
{
    notifier_list_add(&powerdown_notifiers, notifier);
}

void qemu_system_debug_request(void)
{
    debug_requested = 1;
    qemu_notify_event();
}

static bool main_loop_should_exit(void)
{
    RunState r;
    if (qemu_debug_requested()) {
        vm_stop(RUN_STATE_DEBUG);
    }
    if (qemu_suspend_requested()) {
        qemu_system_suspend();
    }
    if (qemu_shutdown_requested()) {
        qemu_kill_report();
        qapi_event_send_shutdown(&error_abort);
        if (no_shutdown) {
            vm_stop(RUN_STATE_SHUTDOWN);
        } else {
            return true;
        }
    }
    if (qemu_reset_requested()) {
        pause_all_vcpus();
        qemu_system_reset(VMRESET_REPORT);
        resume_all_vcpus();
        if (!runstate_check(RUN_STATE_RUNNING) &&
                !runstate_check(RUN_STATE_INMIGRATE)) {
            runstate_set(RUN_STATE_PRELAUNCH);
        }
    }
    if (qemu_wakeup_requested()) {
        pause_all_vcpus();
        qemu_system_reset(VMRESET_SILENT);
        notifier_list_notify(&wakeup_notifiers, &wakeup_reason);
        wakeup_reason = QEMU_WAKEUP_REASON_NONE;
        resume_all_vcpus();
        qapi_event_send_wakeup(&error_abort);
    }
    if (qemu_powerdown_requested()) {
        qemu_system_powerdown();
    }
    if (qemu_vmstop_requested(&r)) {
        vm_stop(r);
    }
    return false;
}

static void main_loop(void)
{
    bool nonblocking;
    int last_io = 0;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif
    do {
        nonblocking = !kvm_enabled() && !xen_enabled() && last_io > 0;
#ifdef CONFIG_PROFILER
        ti = profile_getclock();
#endif
        last_io = main_loop_wait(nonblocking);
#ifdef CONFIG_PROFILER
        dev_time += profile_getclock() - ti;
#endif
    } while (!main_loop_should_exit());
}

static void version(void)
{
    printf("QEMU emulator version " QEMU_VERSION QEMU_PKGVERSION ", Copyright (c) 2003-2008 Fabrice Bellard\n");
}

static void help(int exitcode)
{
    version();
    printf("usage: %s [options] [disk_image]\n\n"
           "'disk_image' is a raw hard disk image for IDE hard disk 0\n\n",
            error_get_progname());

#define QEMU_OPTIONS_GENERATE_HELP
#include "qemu-options-wrapper.h"

    printf("\nDuring emulation, the following keys are useful:\n"
           "ctrl-alt-f      toggle full screen\n"
           "ctrl-alt-n      switch to virtual console 'n'\n"
           "ctrl-alt        toggle mouse and keyboard grab\n"
           "\n"
           "When using -nographic, press 'ctrl-a h' to get some help.\n");

    exit(exitcode);
}

#define HAS_ARG 0x0001

typedef struct QEMUOption {
    const char *name;
    int flags;
    int index;
    uint32_t arch_mask;
} QEMUOption;

static const QEMUOption qemu_options[] = {
    { "h", 0, QEMU_OPTION_h, QEMU_ARCH_ALL },
#define QEMU_OPTIONS_GENERATE_OPTIONS
#include "qemu-options-wrapper.h"
    { NULL },
};

typedef struct VGAInterfaceInfo {
    const char *opt_name;    /* option name */
    const char *name;        /* human-readable name */
    /* Class names indicating that support is available.
     * If no class is specified, the interface is always available */
    const char *class_names[2];
} VGAInterfaceInfo;

static VGAInterfaceInfo vga_interfaces[VGA_TYPE_MAX] = {
    [VGA_NONE] = {
        .opt_name = "none",
    },
    [VGA_STD] = {
        .opt_name = "std",
        .name = "standard VGA",
        .class_names = { "VGA", "isa-vga" },
    },
    [VGA_CIRRUS] = {
        .opt_name = "cirrus",
        .name = "Cirrus VGA",
        .class_names = { "cirrus-vga", "isa-cirrus-vga" },
    },
    [VGA_VMWARE] = {
        .opt_name = "vmware",
        .name = "VMWare SVGA",
        .class_names = { "vmware-svga" },
    },
    [VGA_VIRTIO] = {
        .opt_name = "virtio",
        .name = "Virtio VGA",
        .class_names = { "virtio-vga" },
    },
    [VGA_QXL] = {
        .opt_name = "qxl",
        .name = "QXL VGA",
        .class_names = { "qxl-vga" },
    },
    [VGA_TCX] = {
        .opt_name = "tcx",
        .name = "TCX framebuffer",
        .class_names = { "SUNW,tcx" },
    },
    [VGA_CG3] = {
        .opt_name = "cg3",
        .name = "CG3 framebuffer",
        .class_names = { "cgthree" },
    },
    [VGA_XENFB] = {
        .opt_name = "xenfb",
    },
};

static bool vga_interface_available(VGAInterfaceType t)
{
    VGAInterfaceInfo *ti = &vga_interfaces[t];

    assert(t < VGA_TYPE_MAX);
    return !ti->class_names[0] ||
           object_class_by_name(ti->class_names[0]) ||
           object_class_by_name(ti->class_names[1]);
}

static void select_vgahw(const char *p)
{
    const char *opts;
    int t;

    assert(vga_interface_type == VGA_NONE);
    for (t = 0; t < VGA_TYPE_MAX; t++) {
        VGAInterfaceInfo *ti = &vga_interfaces[t];
        if (ti->opt_name && strstart(p, ti->opt_name, &opts)) {
            if (!vga_interface_available(t)) {
                error_report("%s not available", ti->name);
                exit(1);
            }
            vga_interface_type = t;
            break;
        }
    }
    if (t == VGA_TYPE_MAX) {
    invalid_vga:
        error_report("unknown vga type: %s", p);
        exit(1);
    }
    while (*opts) {
        const char *nextopt;

        if (strstart(opts, ",retrace=", &nextopt)) {
            opts = nextopt;
            if (strstart(opts, "dumb", &nextopt))
                vga_retrace_method = VGA_RETRACE_DUMB;
            else if (strstart(opts, "precise", &nextopt))
                vga_retrace_method = VGA_RETRACE_PRECISE;
            else goto invalid_vga;
        } else goto invalid_vga;
        opts = nextopt;
    }
}

typedef enum DisplayType {
    DT_DEFAULT,
    DT_CURSES,
    DT_SDL,
    DT_COCOA,
    DT_GTK,
    DT_NONE,
} DisplayType;

static DisplayType select_display(const char *p)
{
    const char *opts;
    DisplayType display = DT_DEFAULT;

    if (strstart(p, "sdl", &opts)) {
#ifdef CONFIG_SDL
        display = DT_SDL;
        while (*opts) {
            const char *nextopt;

            if (strstart(opts, ",frame=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    no_frame = 0;
                } else if (strstart(opts, "off", &nextopt)) {
                    no_frame = 1;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",alt_grab=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    alt_grab = 1;
                } else if (strstart(opts, "off", &nextopt)) {
                    alt_grab = 0;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",ctrl_grab=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    ctrl_grab = 1;
                } else if (strstart(opts, "off", &nextopt)) {
                    ctrl_grab = 0;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",window_close=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    no_quit = 0;
                } else if (strstart(opts, "off", &nextopt)) {
                    no_quit = 1;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",gl=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    request_opengl = 1;
                } else if (strstart(opts, "off", &nextopt)) {
                    request_opengl = 0;
                } else {
                    goto invalid_sdl_args;
                }
            } else {
            invalid_sdl_args:
                error_report("invalid SDL option string");
                exit(1);
            }
            opts = nextopt;
        }
#else
        error_report("SDL support is disabled");
        exit(1);
#endif
    } else if (strstart(p, "vnc", &opts)) {
        if (*opts == '=') {
            vnc_parse(opts + 1, &error_fatal);
        } else {
            error_report("VNC requires a display argument vnc=<display>");
            exit(1);
        }
    } else if (strstart(p, "curses", &opts)) {
#ifdef CONFIG_CURSES
        display = DT_CURSES;
#else
        error_report("curses support is disabled");
        exit(1);
#endif
    } else if (strstart(p, "gtk", &opts)) {
#ifdef CONFIG_GTK
        display = DT_GTK;
        while (*opts) {
            const char *nextopt;

            if (strstart(opts, ",grab_on_hover=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    grab_on_hover = true;
                } else if (strstart(opts, "off", &nextopt)) {
                    grab_on_hover = false;
                } else {
                    goto invalid_gtk_args;
                }
            } else if (strstart(opts, ",gl=", &nextopt)) {
                opts = nextopt;
                if (strstart(opts, "on", &nextopt)) {
                    request_opengl = 1;
                } else if (strstart(opts, "off", &nextopt)) {
                    request_opengl = 0;
                } else {
                    goto invalid_gtk_args;
                }
            } else {
            invalid_gtk_args:
                error_report("invalid GTK option string");
                exit(1);
            }
            opts = nextopt;
        }
#else
        error_report("GTK support is disabled");
        exit(1);
#endif
    } else if (strstart(p, "none", &opts)) {
        display = DT_NONE;
    } else {
        error_report("unknown display type");
        exit(1);
    }

    return display;
}

static int balloon_parse(const char *arg)
{
    QemuOpts *opts;

    if (strcmp(arg, "none") == 0) {
        return 0;
    }

    if (!strncmp(arg, "virtio", 6)) {
        if (arg[6] == ',') {
            /* have params -> parse them */
            opts = qemu_opts_parse_noisily(qemu_find_opts("device"), arg + 7,
                                           false);
            if (!opts)
                return  -1;
        } else {
            /* create empty opts */
            opts = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                    &error_abort);
        }
        qemu_opt_set(opts, "driver", "virtio-balloon", &error_abort);
        return 0;
    }

    return -1;
}

char *qemu_find_file(int type, const char *name)
{
    int i;
    const char *subdir;
    char *buf;

    /* Try the name as a straight path first */
    if (access(name, R_OK) == 0) {
        trace_load_file(name, name);
        return g_strdup(name);
    }

    switch (type) {
    case QEMU_FILE_TYPE_BIOS:
        subdir = "";
        break;
    case QEMU_FILE_TYPE_KEYMAP:
        subdir = "keymaps/";
        break;
    default:
        abort();
    }

    for (i = 0; i < data_dir_idx; i++) {
        buf = g_strdup_printf("%s/%s%s", data_dir[i], subdir, name);
        if (access(buf, R_OK) == 0) {
            trace_load_file(name, buf);
            return buf;
        }
        g_free(buf);
    }
    return NULL;
}

static inline bool nonempty_str(const char *str)
{
    return str && *str;
}

static int parse_fw_cfg(void *opaque, QemuOpts *opts, Error **errp)
{
    gchar *buf;
    size_t size;
    const char *name, *file, *str;
    FWCfgState *fw_cfg = (FWCfgState *) opaque;

    if (fw_cfg == NULL) {
        error_report("fw_cfg device not available");
        return -1;
    }
    name = qemu_opt_get(opts, "name");
    file = qemu_opt_get(opts, "file");
    str = qemu_opt_get(opts, "string");

    /* we need name and either a file or the content string */
    if (!(nonempty_str(name) && (nonempty_str(file) || nonempty_str(str)))) {
        error_report("invalid argument(s)");
        return -1;
    }
    if (nonempty_str(file) && nonempty_str(str)) {
        error_report("file and string are mutually exclusive");
        return -1;
    }
    if (strlen(name) > FW_CFG_MAX_FILE_PATH - 1) {
        error_report("name too long (max. %d char)", FW_CFG_MAX_FILE_PATH - 1);
        return -1;
    }
    if (strncmp(name, "opt/", 4) != 0) {
        error_report("warning: externally provided fw_cfg item names "
                     "should be prefixed with \"opt/\"");
    }
    if (nonempty_str(str)) {
        size = strlen(str); /* NUL terminator NOT included in fw_cfg blob */
        buf = g_memdup(str, size);
    } else {
        if (!g_file_get_contents(file, &buf, &size, NULL)) {
            error_report("can't load %s", file);
            return -1;
        }
    }
    /* For legacy, keep user files in a specific global order. */
    fw_cfg_set_order_override(fw_cfg, FW_CFG_ORDER_OVERRIDE_USER);
    fw_cfg_add_file(fw_cfg, name, buf, size);
    fw_cfg_reset_order_override(fw_cfg);
    return 0;
}

static int device_help_func(void *opaque, QemuOpts *opts, Error **errp)
{
    return qdev_device_help(opts);
}

static int device_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *err = NULL;
    DeviceState *dev;

    dev = qdev_device_add(opts, &err);
    if (!dev) {
        error_report_err(err);
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}

static int chardev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;

    qemu_chr_new_from_opts(opts, NULL, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }
    return 0;
}

#ifdef CONFIG_VIRTFS
static int fsdev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    int ret;
    ret = qemu_fsdev_add(opts);

    return ret;
}
#endif

static int mon_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    CharDriverState *chr;
    const char *chardev;
    const char *mode;
    int flags;

    mode = qemu_opt_get(opts, "mode");
    if (mode == NULL) {
        mode = "readline";
    }
    if (strcmp(mode, "readline") == 0) {
        flags = MONITOR_USE_READLINE;
    } else if (strcmp(mode, "control") == 0) {
        flags = MONITOR_USE_CONTROL;
    } else {
        error_report("unknown monitor mode \"%s\"", mode);
        exit(1);
    }

    if (qemu_opt_get_bool(opts, "pretty", 0))
        flags |= MONITOR_USE_PRETTY;

    if (qemu_opt_get_bool(opts, "default", 0))
        flags |= MONITOR_IS_DEFAULT;

    chardev = qemu_opt_get(opts, "chardev");
    chr = qemu_chr_find(chardev);
    if (chr == NULL) {
        error_report("chardev \"%s\" not found", chardev);
        exit(1);
    }

    qemu_chr_fe_claim_no_fail(chr);
    monitor_init(chr, flags);
    return 0;
}

static void monitor_parse(const char *optarg, const char *mode, bool pretty)
{
    static int monitor_device_index = 0;
    QemuOpts *opts;
    const char *p;
    char label[32];
    int def = 0;

    if (strstart(optarg, "chardev:", &p)) {
        snprintf(label, sizeof(label), "%s", p);
    } else {
        snprintf(label, sizeof(label), "compat_monitor%d",
                 monitor_device_index);
        if (monitor_device_index == 0) {
            def = 1;
        }
        opts = qemu_chr_parse_compat(label, optarg);
        if (!opts) {
            error_report("parse error: %s", optarg);
            exit(1);
        }
    }

    opts = qemu_opts_create(qemu_find_opts("mon"), label, 1, &error_fatal);
    qemu_opt_set(opts, "mode", mode, &error_abort);
    qemu_opt_set(opts, "chardev", label, &error_abort);
    qemu_opt_set_bool(opts, "pretty", pretty, &error_abort);
    if (def)
        qemu_opt_set(opts, "default", "on", &error_abort);
    monitor_device_index++;
}

struct device_config {
    enum {
        DEV_USB,       /* -usbdevice     */
        DEV_BT,        /* -bt            */
        DEV_SERIAL,    /* -serial        */
        DEV_PARALLEL,  /* -parallel      */
        DEV_VIRTCON,   /* -virtioconsole */
        DEV_DEBUGCON,  /* -debugcon */
        DEV_GDB,       /* -gdb, -s */
        DEV_SCLP,      /* s390 sclp */
    } type;
    const char *cmdline;
    Location loc;
    QTAILQ_ENTRY(device_config) next;
};

static QTAILQ_HEAD(, device_config) device_configs =
    QTAILQ_HEAD_INITIALIZER(device_configs);

static void add_device_config(int type, const char *cmdline)
{
    struct device_config *conf;

    conf = g_malloc0(sizeof(*conf));
    conf->type = type;
    conf->cmdline = cmdline;
    loc_save(&conf->loc);
    QTAILQ_INSERT_TAIL(&device_configs, conf, next);
}

static int foreach_device_config(int type, int (*func)(const char *cmdline))
{
    struct device_config *conf;
    int rc;

    QTAILQ_FOREACH(conf, &device_configs, next) {
        if (conf->type != type)
            continue;
        loc_push_restore(&conf->loc);
        rc = func(conf->cmdline);
        loc_pop(&conf->loc);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

static int serial_parse(const char *devname)
{
    static int index = 0;
    char label[32];

    if (strcmp(devname, "none") == 0)
        return 0;
    if (index == MAX_SERIAL_PORTS) {
        error_report("too many serial ports");
        exit(1);
    }
    snprintf(label, sizeof(label), "serial%d", index);
    serial_hds[index] = qemu_chr_new(label, devname, NULL);
    if (!serial_hds[index]) {
        error_report("could not connect serial device"
                     " to character backend '%s'", devname);
        return -1;
    }
    index++;
    return 0;
}

static int parallel_parse(const char *devname)
{
    static int index = 0;
    char label[32];

    if (strcmp(devname, "none") == 0)
        return 0;
    if (index == MAX_PARALLEL_PORTS) {
        error_report("too many parallel ports");
        exit(1);
    }
    snprintf(label, sizeof(label), "parallel%d", index);
    parallel_hds[index] = qemu_chr_new(label, devname, NULL);
    if (!parallel_hds[index]) {
        error_report("could not connect parallel device"
                     " to character backend '%s'", devname);
        return -1;
    }
    index++;
    return 0;
}

static int virtcon_parse(const char *devname)
{
    QemuOptsList *device = qemu_find_opts("device");
    static int index = 0;
    char label[32];
    QemuOpts *bus_opts, *dev_opts;

    if (strcmp(devname, "none") == 0)
        return 0;
    if (index == MAX_VIRTIO_CONSOLES) {
        error_report("too many virtio consoles");
        exit(1);
    }

    bus_opts = qemu_opts_create(device, NULL, 0, &error_abort);
    qemu_opt_set(bus_opts, "driver", "virtio-serial", &error_abort);

    dev_opts = qemu_opts_create(device, NULL, 0, &error_abort);
    qemu_opt_set(dev_opts, "driver", "virtconsole", &error_abort);

    snprintf(label, sizeof(label), "virtcon%d", index);
    virtcon_hds[index] = qemu_chr_new(label, devname, NULL);
    if (!virtcon_hds[index]) {
        error_report("could not connect virtio console"
                     " to character backend '%s'", devname);
        return -1;
    }
    qemu_opt_set(dev_opts, "chardev", label, &error_abort);

    index++;
    return 0;
}

static int sclp_parse(const char *devname)
{
    QemuOptsList *device = qemu_find_opts("device");
    static int index = 0;
    char label[32];
    QemuOpts *dev_opts;

    if (strcmp(devname, "none") == 0) {
        return 0;
    }
    if (index == MAX_SCLP_CONSOLES) {
        error_report("too many sclp consoles");
        exit(1);
    }

    assert(arch_type == QEMU_ARCH_S390X);

    dev_opts = qemu_opts_create(device, NULL, 0, NULL);
    qemu_opt_set(dev_opts, "driver", "sclpconsole", &error_abort);

    snprintf(label, sizeof(label), "sclpcon%d", index);
    sclp_hds[index] = qemu_chr_new(label, devname, NULL);
    if (!sclp_hds[index]) {
        error_report("could not connect sclp console"
                     " to character backend '%s'", devname);
        return -1;
    }
    qemu_opt_set(dev_opts, "chardev", label, &error_abort);

    index++;
    return 0;
}

static int debugcon_parse(const char *devname)
{
    QemuOpts *opts;

    if (!qemu_chr_new("debugcon", devname, NULL)) {
        exit(1);
    }
    opts = qemu_opts_create(qemu_find_opts("device"), "debugcon", 1, NULL);
    if (!opts) {
        error_report("already have a debugcon device");
        exit(1);
    }
    qemu_opt_set(opts, "driver", "isa-debugcon", &error_abort);
    qemu_opt_set(opts, "chardev", "debugcon", &error_abort);
    return 0;
}

static gint machine_class_cmp(gconstpointer a, gconstpointer b)
{
    const MachineClass *mc1 = a, *mc2 = b;
    int res;

    if (mc1->family == NULL) {
        if (mc2->family == NULL) {
            /* Compare standalone machine types against each other; they sort
             * in increasing order.
             */
            return strcmp(object_class_get_name(OBJECT_CLASS(mc1)),
                          object_class_get_name(OBJECT_CLASS(mc2)));
        }

        /* Standalone machine types sort after families. */
        return 1;
    }

    if (mc2->family == NULL) {
        /* Families sort before standalone machine types. */
        return -1;
    }

    /* Families sort between each other alphabetically increasingly. */
    res = strcmp(mc1->family, mc2->family);
    if (res != 0) {
        return res;
    }

    /* Within the same family, machine types sort in decreasing order. */
    return strcmp(object_class_get_name(OBJECT_CLASS(mc2)),
                  object_class_get_name(OBJECT_CLASS(mc1)));
}

 static MachineClass *machine_parse(const char *name)
{
    MachineClass *mc = NULL;
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);

    if (name) {
        mc = find_machine(name);
    }
    if (mc) {
        g_slist_free(machines);
        return mc;
    }
    if (name && !is_help_option(name)) {
        error_report("unsupported machine type");
        error_printf("Use -machine help to list supported machines\n");
    } else {
        printf("Supported machines are:\n");
        machines = g_slist_sort(machines, machine_class_cmp);
        for (el = machines; el; el = el->next) {
            MachineClass *mc = el->data;
            if (mc->alias) {
                printf("%-20s %s (alias of %s)\n", mc->alias, mc->desc, mc->name);
            }
            printf("%-20s %s%s\n", mc->name, mc->desc,
                   mc->is_default ? " (default)" : "");
        }
    }

    g_slist_free(machines);
    exit(!name || !is_help_option(name));
}

void qemu_add_exit_notifier(Notifier *notify)
{
    notifier_list_add(&exit_notifiers, notify);
}

void qemu_remove_exit_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

static void qemu_run_exit_notifiers(void)
{
    notifier_list_notify(&exit_notifiers, NULL);
}

static bool machine_init_done;

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
    notifier_list_add(&machine_init_done_notifiers, notify);
    if (machine_init_done) {
        notify->notify(notify, NULL);
    }
}

static void qemu_run_machine_init_done_notifiers(void)
{
    notifier_list_notify(&machine_init_done_notifiers, NULL);
    machine_init_done = true;
}

static const QEMUOption *lookup_opt(int argc, char **argv,
                                    const char **poptarg, int *poptind)
{
    const QEMUOption *popt;
    int optind = *poptind;
    char *r = argv[optind];
    const char *optarg;

    loc_set_cmdline(argv, optind, 1);
    optind++;
    /* Treat --foo the same as -foo.  */
    if (r[1] == '-')
        r++;
    popt = qemu_options;
    for(;;) {
        if (!popt->name) {
            error_report("invalid option");
            exit(1);
        }
        if (!strcmp(popt->name, r + 1))
            break;
        popt++;
    }
    if (popt->flags & HAS_ARG) {
        if (optind >= argc) {
            error_report("requires an argument");
            exit(1);
        }
        optarg = argv[optind++];
        loc_set_cmdline(argv, optind - 2, 2);
    } else {
        optarg = NULL;
    }

    *poptarg = optarg;
    *poptind = optind;

    return popt;
}

static MachineClass *select_machine(void)
{
    MachineClass *machine_class = find_default_machine();
    const char *optarg;
    QemuOpts *opts;
    Location loc;

    loc_push_none(&loc);

    opts = qemu_get_machine_opts();
    qemu_opts_loc_restore(opts);

    optarg = qemu_opt_get(opts, "type");
    if (optarg) {
        machine_class = machine_parse(optarg);
    }

    if (!machine_class) {
        error_report("No machine specified, and there is no default");
        error_printf("Use -machine help to list supported machines\n");
        exit(1);
    }

    loc_pop(&loc);
    return machine_class;
}

static int machine_set_property(void *opaque,
                                const char *name, const char *value,
                                Error **errp)
{
    Object *obj = OBJECT(opaque);
    Error *local_err = NULL;
    char *c, *qom_name;

    if (strcmp(name, "type") == 0) {
        return 0;
    }

    qom_name = g_strdup(name);
    c = qom_name;
    while (*c++) {
        if (*c == '_') {
            *c = '-';
        }
    }

    object_property_parse(obj, value, qom_name, &local_err);
    g_free(qom_name);

    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return 0;
}


/*
 * Initial object creation happens before all other
 * QEMU data types are created. The majority of objects
 * can be created at this point. The rng-egd object
 * cannot be created here, as it depends on the chardev
 * already existing.
 */
static bool object_create_initial(const char *type)
{
    if (g_str_equal(type, "rng-egd")) {
        return false;
    }

    /*
     * return false for concrete netfilters since
     * they depend on netdevs already existing
     */
    if (g_str_equal(type, "filter-buffer") ||
        g_str_equal(type, "filter-dump") ||
        g_str_equal(type, "filter-mirror") ||
        g_str_equal(type, "filter-redirector")) {
        return false;
    }

    return true;
}


/*
 * The remainder of object creation happens after the
 * creation of chardev, fsdev, net clients and device data types.
 */
static bool object_create_delayed(const char *type)
{
    return !object_create_initial(type);
}


static void set_memory_options(uint64_t *ram_slots, ram_addr_t *maxram_size,
                               MachineClass *mc)
{
    uint64_t sz;
    const char *mem_str;
    const char *maxmem_str, *slots_str;
    const ram_addr_t default_ram_size = mc->default_ram_size;
    QemuOpts *opts = qemu_find_opts_singleton("memory");
    Location loc;

    loc_push_none(&loc);
    qemu_opts_loc_restore(opts);

    sz = 0;
    mem_str = qemu_opt_get(opts, "size");
    if (mem_str) {
        if (!*mem_str) {
            error_report("missing 'size' option value");
            exit(EXIT_FAILURE);
        }

        sz = qemu_opt_get_size(opts, "size", ram_size);

        /* Fix up legacy suffix-less format */
        if (g_ascii_isdigit(mem_str[strlen(mem_str) - 1])) {
            uint64_t overflow_check = sz;

            sz <<= 20;
            if ((sz >> 20) != overflow_check) {
                error_report("too large 'size' option value");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* backward compatibility behaviour for case "-m 0" */
    if (sz == 0) {
        sz = default_ram_size;
    }

    sz = QEMU_ALIGN_UP(sz, 8192);
    ram_size = sz;
    if (ram_size != sz) {
        error_report("ram size too large");
        exit(EXIT_FAILURE);
    }

    /* store value for the future use */
    qemu_opt_set_number(opts, "size", ram_size, &error_abort);
    *maxram_size = ram_size;

    maxmem_str = qemu_opt_get(opts, "maxmem");
    slots_str = qemu_opt_get(opts, "slots");
    if (maxmem_str && slots_str) {
        uint64_t slots;

        sz = qemu_opt_get_size(opts, "maxmem", 0);
        slots = qemu_opt_get_number(opts, "slots", 0);
        if (sz < ram_size) {
            error_report("invalid value of -m option maxmem: "
                         "maximum memory size (0x%" PRIx64 ") must be at least "
                         "the initial memory size (0x" RAM_ADDR_FMT ")",
                         sz, ram_size);
            exit(EXIT_FAILURE);
        } else if (sz > ram_size) {
            if (!slots) {
                error_report("invalid value of -m option: maxmem was "
                             "specified, but no hotplug slots were specified");
                exit(EXIT_FAILURE);
            }
        } else if (slots) {
            error_report("invalid value of -m option maxmem: "
                         "memory slots were specified but maximum memory size "
                         "(0x%" PRIx64 ") is equal to the initial memory size "
                         "(0x" RAM_ADDR_FMT ")", sz, ram_size);
            exit(EXIT_FAILURE);
        }

        *maxram_size = sz;
        *ram_slots = slots;
    } else if ((!maxmem_str && slots_str) ||
            (maxmem_str && !slots_str)) {
        error_report("invalid -m option value: missing "
                "'%s' option", slots_str ? "maxmem" : "slots");
        exit(EXIT_FAILURE);
    }

    loc_pop(&loc);
}

int main(int argc, char **argv, char **envp)
{
    int i;
    int snapshot, linux_boot;
    const char *initrd_filename;
    const char *kernel_filename, *kernel_cmdline;
    const char *boot_order = NULL;
    const char *boot_once = NULL;
    DisplayState *ds;
    int cyls, heads, secs, translation;
    QemuOpts *hda_opts = NULL, *opts, *machine_opts, *icount_opts = NULL;
    QemuOptsList *olist;
    int optind;
    const char *optarg;
    const char *loadvm = NULL;
    MachineClass *machine_class;
    const char *cpu_model;
    const char *vga_model = NULL;
    const char *qtest_chrdev = NULL;
    const char *qtest_log = NULL;
    const char *pid_file = NULL;
    const char *incoming = NULL;
    int show_vnc_port = 0;
    bool defconfig = true;
    bool userconfig = true;
    bool nographic = false;
    DisplayType display_type = DT_DEFAULT;
    int display_remote = 0;
    const char *log_mask = NULL;
    const char *log_file = NULL;
    char *trace_file = NULL;
    ram_addr_t maxram_size;
    uint64_t ram_slots = 0;
    FILE *vmstate_dump_file = NULL;
    Error *main_loop_err = NULL;
    Error *err = NULL;

    qemu_init_cpu_loop();
    qemu_mutex_lock_iothread();

    atexit(qemu_run_exit_notifiers);
    error_set_progname(argv[0]);
    qemu_init_exec_dir(argv[0]);

    module_call_init(MODULE_INIT_QOM);

    qemu_add_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&qemu_legacy_drive_opts);
    qemu_add_drive_opts(&qemu_common_drive_opts);
    qemu_add_drive_opts(&qemu_drive_opts);
    qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);
    qemu_add_opts(&qemu_netdev_opts);
    qemu_add_opts(&qemu_net_opts);
    qemu_add_opts(&qemu_rtc_opts);
    qemu_add_opts(&qemu_global_opts);
    qemu_add_opts(&qemu_mon_opts);
    qemu_add_opts(&qemu_trace_opts);
    qemu_add_opts(&qemu_option_rom_opts);
    qemu_add_opts(&qemu_machine_opts);
    qemu_add_opts(&qemu_mem_opts);
    qemu_add_opts(&qemu_smp_opts);
    qemu_add_opts(&qemu_boot_opts);
    qemu_add_opts(&qemu_sandbox_opts);
    qemu_add_opts(&qemu_add_fd_opts);
    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_tpmdev_opts);
    qemu_add_opts(&qemu_realtime_opts);
    qemu_add_opts(&qemu_msg_opts);
    qemu_add_opts(&qemu_name_opts);
    qemu_add_opts(&qemu_numa_opts);
    qemu_add_opts(&qemu_icount_opts);
    qemu_add_opts(&qemu_semihosting_config_opts);
    qemu_add_opts(&qemu_fw_cfg_opts);
    module_call_init(MODULE_INIT_OPTS);

    runstate_init();

    if (qcrypto_init(&err) < 0) {
        error_reportf_err(err, "cannot initialize crypto: ");
        exit(1);
    }
    rtc_clock = QEMU_CLOCK_HOST;

    QLIST_INIT (&vm_change_state_head);
    os_setup_early_signal_handling();

    cpu_model = NULL;
    snapshot = 0;
    cyls = heads = secs = 0;
    translation = BIOS_ATA_TRANSLATION_AUTO;

    nb_nics = 0;

    bdrv_init_with_whitelist();

    autostart = 1;

    /* first pass of option parsing */
    optind = 1;
    while (optind < argc) {
        if (argv[optind][0] != '-') {
            /* disk image */
            optind++;
        } else {
            const QEMUOption *popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            switch (popt->index) {
            case QEMU_OPTION_nodefconfig:
                defconfig = false;
                break;
            case QEMU_OPTION_nouserconfig:
                userconfig = false;
                break;
            }
        }
    }

    if (defconfig) {
        int ret;
        ret = qemu_read_default_config_files(userconfig);
        if (ret < 0) {
            exit(1);
        }
    }

    /* second pass of option parsing */
    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        if (argv[optind][0] != '-') {
            hda_opts = drive_add(IF_DEFAULT, 0, argv[optind++], HD_OPTS);
        } else {
            const QEMUOption *popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            if (!(popt->arch_mask & arch_type)) {
                printf("Option %s not supported for this target\n", popt->name);
                exit(1);
            }
            switch(popt->index) {
            case QEMU_OPTION_no_kvm_irqchip: {
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "kernel_irqchip=off", false);
                break;
            }
            case QEMU_OPTION_cpu:
                /* hw initialization will check this */
                cpu_model = optarg;
                break;
            case QEMU_OPTION_hda:
                {
                    char buf[256];
                    if (cyls == 0)
                        snprintf(buf, sizeof(buf), "%s", HD_OPTS);
                    else
                        snprintf(buf, sizeof(buf),
                                 "%s,cyls=%d,heads=%d,secs=%d%s",
                                 HD_OPTS , cyls, heads, secs,
                                 translation == BIOS_ATA_TRANSLATION_LBA ?
                                 ",trans=lba" :
                                 translation == BIOS_ATA_TRANSLATION_NONE ?
                                 ",trans=none" : "");
                    drive_add(IF_DEFAULT, 0, optarg, buf);
                    break;
                }
            case QEMU_OPTION_hdb:
            case QEMU_OPTION_hdc:
            case QEMU_OPTION_hdd:
                drive_add(IF_DEFAULT, popt->index - QEMU_OPTION_hda, optarg,
                          HD_OPTS);
                break;
            case QEMU_OPTION_drive:
                if (drive_def(optarg) == NULL) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_set:
                if (qemu_set_option(optarg) != 0)
                    exit(1);
                break;
            case QEMU_OPTION_global:
                if (qemu_global_option(optarg) != 0)
                    exit(1);
                break;
            case QEMU_OPTION_mtdblock:
                drive_add(IF_MTD, -1, optarg, MTD_OPTS);
                break;
            case QEMU_OPTION_sd:
                drive_add(IF_SD, -1, optarg, SD_OPTS);
                break;
            case QEMU_OPTION_pflash:
                drive_add(IF_PFLASH, -1, optarg, PFLASH_OPTS);
                break;
            case QEMU_OPTION_snapshot:
                snapshot = 1;
                break;
            case QEMU_OPTION_hdachs:
                {
                    const char *p;
                    p = optarg;
                    cyls = strtol(p, (char **)&p, 0);
                    if (cyls < 1 || cyls > 16383)
                        goto chs_fail;
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    heads = strtol(p, (char **)&p, 0);
                    if (heads < 1 || heads > 16)
                        goto chs_fail;
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    secs = strtol(p, (char **)&p, 0);
                    if (secs < 1 || secs > 63)
                        goto chs_fail;
                    if (*p == ',') {
                        p++;
                        if (!strcmp(p, "large")) {
                            translation = BIOS_ATA_TRANSLATION_LARGE;
                        } else if (!strcmp(p, "rechs")) {
                            translation = BIOS_ATA_TRANSLATION_RECHS;
                        } else if (!strcmp(p, "none")) {
                            translation = BIOS_ATA_TRANSLATION_NONE;
                        } else if (!strcmp(p, "lba")) {
                            translation = BIOS_ATA_TRANSLATION_LBA;
                        } else if (!strcmp(p, "auto")) {
                            translation = BIOS_ATA_TRANSLATION_AUTO;
                        } else {
                            goto chs_fail;
                        }
                    } else if (*p != '\0') {
                    chs_fail:
                        error_report("invalid physical CHS format");
                        exit(1);
                    }
                    if (hda_opts != NULL) {
                        qemu_opt_set_number(hda_opts, "cyls", cyls,
                                            &error_abort);
                        qemu_opt_set_number(hda_opts, "heads", heads,
                                            &error_abort);
                        qemu_opt_set_number(hda_opts, "secs", secs,
                                            &error_abort);
                        if (translation == BIOS_ATA_TRANSLATION_LARGE) {
                            qemu_opt_set(hda_opts, "trans", "large",
                                         &error_abort);
                        } else if (translation == BIOS_ATA_TRANSLATION_RECHS) {
                            qemu_opt_set(hda_opts, "trans", "rechs",
                                         &error_abort);
                        } else if (translation == BIOS_ATA_TRANSLATION_LBA) {
                            qemu_opt_set(hda_opts, "trans", "lba",
                                         &error_abort);
                        } else if (translation == BIOS_ATA_TRANSLATION_NONE) {
                            qemu_opt_set(hda_opts, "trans", "none",
                                         &error_abort);
                        }
                    }
                }
                break;
            case QEMU_OPTION_numa:
                opts = qemu_opts_parse_noisily(qemu_find_opts("numa"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_display:
                display_type = select_display(optarg);
                break;
            case QEMU_OPTION_nographic:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "graphics=off", false);
                nographic = true;
                display_type = DT_NONE;
                break;
            case QEMU_OPTION_curses:
#ifdef CONFIG_CURSES
                display_type = DT_CURSES;
#else
                error_report("curses support is disabled");
                exit(1);
#endif
                break;
            case QEMU_OPTION_portrait:
                graphic_rotate = 90;
                break;
            case QEMU_OPTION_rotate:
                graphic_rotate = strtol(optarg, (char **) &optarg, 10);
                if (graphic_rotate != 0 && graphic_rotate != 90 &&
                    graphic_rotate != 180 && graphic_rotate != 270) {
                    error_report("only 90, 180, 270 deg rotation is available");
                    exit(1);
                }
                break;
            case QEMU_OPTION_kernel:
                qemu_opts_set(qemu_find_opts("machine"), 0, "kernel", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_initrd:
                qemu_opts_set(qemu_find_opts("machine"), 0, "initrd", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_append:
                qemu_opts_set(qemu_find_opts("machine"), 0, "append", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_dtb:
                qemu_opts_set(qemu_find_opts("machine"), 0, "dtb", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_cdrom:
                drive_add(IF_DEFAULT, 2, optarg, CDROM_OPTS);
                break;
            case QEMU_OPTION_boot:
                opts = qemu_opts_parse_noisily(qemu_find_opts("boot-opts"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_fda:
            case QEMU_OPTION_fdb:
                drive_add(IF_FLOPPY, popt->index - QEMU_OPTION_fda,
                          optarg, FD_OPTS);
                break;
            case QEMU_OPTION_no_fd_bootchk:
                fd_bootchk = 0;
                break;
            case QEMU_OPTION_netdev:
                default_net = 0;
                if (net_client_parse(qemu_find_opts("netdev"), optarg) == -1) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_net:
                default_net = 0;
                if (net_client_parse(qemu_find_opts("net"), optarg) == -1) {
                    exit(1);
                }
                break;
#ifdef CONFIG_LIBISCSI
            case QEMU_OPTION_iscsi:
                opts = qemu_opts_parse_noisily(qemu_find_opts("iscsi"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                break;
#endif
#ifdef CONFIG_SLIRP
            case QEMU_OPTION_tftp:
                error_report("The -tftp option is deprecated. "
                             "Please use '-netdev user,tftp=...' instead.");
                legacy_tftp_prefix = optarg;
                break;
            case QEMU_OPTION_bootp:
                error_report("The -bootp option is deprecated. "
                             "Please use '-netdev user,bootfile=...' instead.");
                legacy_bootp_filename = optarg;
                break;
            case QEMU_OPTION_redir:
                error_report("The -redir option is deprecated. "
                             "Please use '-netdev user,hostfwd=...' instead.");
                if (net_slirp_redir(optarg) < 0)
                    exit(1);
                break;
#endif
            case QEMU_OPTION_bt:
                add_device_config(DEV_BT, optarg);
                break;
            case QEMU_OPTION_audio_help:
                AUD_help ();
                exit (0);
                break;
            case QEMU_OPTION_soundhw:
                select_soundhw (optarg);
                break;
            case QEMU_OPTION_h:
                help(0);
                break;
            case QEMU_OPTION_version:
                version();
                exit(0);
                break;
            case QEMU_OPTION_m:
                opts = qemu_opts_parse_noisily(qemu_find_opts("memory"),
                                               optarg, true);
                if (!opts) {
                    exit(EXIT_FAILURE);
                }
                break;
#ifdef CONFIG_TPM
            case QEMU_OPTION_tpmdev:
                if (tpm_config_parse(qemu_find_opts("tpmdev"), optarg) < 0) {
                    exit(1);
                }
                break;
#endif
            case QEMU_OPTION_mempath:
                mem_path = optarg;
                break;
            case QEMU_OPTION_mem_prealloc:
                mem_prealloc = 1;
                break;
            case QEMU_OPTION_d:
                log_mask = optarg;
                break;
            case QEMU_OPTION_D:
                log_file = optarg;
                break;
            case QEMU_OPTION_DFILTER:
                qemu_set_dfilter_ranges(optarg);
                break;
            case QEMU_OPTION_s:
                add_device_config(DEV_GDB, "tcp::" DEFAULT_GDBSTUB_PORT);
                break;
            case QEMU_OPTION_gdb:
                add_device_config(DEV_GDB, optarg);
                break;
            case QEMU_OPTION_L:
                if (data_dir_idx < ARRAY_SIZE(data_dir)) {
                    data_dir[data_dir_idx++] = optarg;
                }
                break;
            case QEMU_OPTION_bios:
                qemu_opts_set(qemu_find_opts("machine"), 0, "firmware", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_singlestep:
                singlestep = 1;
                break;
            case QEMU_OPTION_S:
                autostart = 0;
                break;
            case QEMU_OPTION_k:
                keyboard_layout = optarg;
                break;
            case QEMU_OPTION_localtime:
                rtc_utc = 0;
                break;
            case QEMU_OPTION_vga:
                vga_model = optarg;
                default_vga = 0;
                break;
            case QEMU_OPTION_g:
                {
                    const char *p;
                    int w, h, depth;
                    p = optarg;
                    w = strtol(p, (char **)&p, 10);
                    if (w <= 0) {
                    graphic_error:
                        error_report("invalid resolution or depth");
                        exit(1);
                    }
                    if (*p != 'x')
                        goto graphic_error;
                    p++;
                    h = strtol(p, (char **)&p, 10);
                    if (h <= 0)
                        goto graphic_error;
                    if (*p == 'x') {
                        p++;
                        depth = strtol(p, (char **)&p, 10);
                        if (depth != 8 && depth != 15 && depth != 16 &&
                            depth != 24 && depth != 32)
                            goto graphic_error;
                    } else if (*p == '\0') {
                        depth = graphic_depth;
                    } else {
                        goto graphic_error;
                    }

                    graphic_width = w;
                    graphic_height = h;
                    graphic_depth = depth;
                }
                break;
            case QEMU_OPTION_echr:
                {
                    char *r;
                    term_escape_char = strtol(optarg, &r, 0);
                    if (r == optarg)
                        printf("Bad argument to echr\n");
                    break;
                }
            case QEMU_OPTION_monitor:
                default_monitor = 0;
                if (strncmp(optarg, "none", 4)) {
                    monitor_parse(optarg, "readline", false);
                }
                break;
            case QEMU_OPTION_qmp:
                monitor_parse(optarg, "control", false);
                default_monitor = 0;
                break;
            case QEMU_OPTION_qmp_pretty:
                monitor_parse(optarg, "control", true);
                default_monitor = 0;
                break;
            case QEMU_OPTION_mon:
                opts = qemu_opts_parse_noisily(qemu_find_opts("mon"), optarg,
                                               true);
                if (!opts) {
                    exit(1);
                }
                default_monitor = 0;
                break;
            case QEMU_OPTION_chardev:
                opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_fsdev:
                olist = qemu_find_opts("fsdev");
                if (!olist) {
                    error_report("fsdev support is disabled");
                    exit(1);
                }
                opts = qemu_opts_parse_noisily(olist, optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_virtfs: {
                QemuOpts *fsdev;
                QemuOpts *device;
                const char *writeout, *sock_fd, *socket;

                olist = qemu_find_opts("virtfs");
                if (!olist) {
                    error_report("virtfs support is disabled");
                    exit(1);
                }
                opts = qemu_opts_parse_noisily(olist, optarg, true);
                if (!opts) {
                    exit(1);
                }

                if (qemu_opt_get(opts, "fsdriver") == NULL ||
                    qemu_opt_get(opts, "mount_tag") == NULL) {
                    error_report("Usage: -virtfs fsdriver,mount_tag=tag");
                    exit(1);
                }
                fsdev = qemu_opts_create(qemu_find_opts("fsdev"),
                                         qemu_opt_get(opts, "mount_tag"),
                                         1, NULL);
                if (!fsdev) {
                    error_report("duplicate fsdev id: %s",
                                 qemu_opt_get(opts, "mount_tag"));
                    exit(1);
                }

                writeout = qemu_opt_get(opts, "writeout");
                if (writeout) {
#ifdef CONFIG_SYNC_FILE_RANGE
                    qemu_opt_set(fsdev, "writeout", writeout, &error_abort);
#else
                    error_report("writeout=immediate not supported "
                                 "on this platform");
                    exit(1);
#endif
                }
                qemu_opt_set(fsdev, "fsdriver",
                             qemu_opt_get(opts, "fsdriver"), &error_abort);
                qemu_opt_set(fsdev, "path", qemu_opt_get(opts, "path"),
                             &error_abort);
                qemu_opt_set(fsdev, "security_model",
                             qemu_opt_get(opts, "security_model"),
                             &error_abort);
                socket = qemu_opt_get(opts, "socket");
                if (socket) {
                    qemu_opt_set(fsdev, "socket", socket, &error_abort);
                }
                sock_fd = qemu_opt_get(opts, "sock_fd");
                if (sock_fd) {
                    qemu_opt_set(fsdev, "sock_fd", sock_fd, &error_abort);
                }

                qemu_opt_set_bool(fsdev, "readonly",
                                  qemu_opt_get_bool(opts, "readonly", 0),
                                  &error_abort);
                device = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                          &error_abort);
                qemu_opt_set(device, "driver", "virtio-9p-pci", &error_abort);
                qemu_opt_set(device, "fsdev",
                             qemu_opt_get(opts, "mount_tag"), &error_abort);
                qemu_opt_set(device, "mount_tag",
                             qemu_opt_get(opts, "mount_tag"), &error_abort);
                break;
            }
            case QEMU_OPTION_virtfs_synth: {
                QemuOpts *fsdev;
                QemuOpts *device;

                fsdev = qemu_opts_create(qemu_find_opts("fsdev"), "v_synth",
                                         1, NULL);
                if (!fsdev) {
                    error_report("duplicate option: %s", "virtfs_synth");
                    exit(1);
                }
                qemu_opt_set(fsdev, "fsdriver", "synth", &error_abort);

                device = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                          &error_abort);
                qemu_opt_set(device, "driver", "virtio-9p-pci", &error_abort);
                qemu_opt_set(device, "fsdev", "v_synth", &error_abort);
                qemu_opt_set(device, "mount_tag", "v_synth", &error_abort);
                break;
            }
            case QEMU_OPTION_serial:
                add_device_config(DEV_SERIAL, optarg);
                default_serial = 0;
                if (strncmp(optarg, "mon:", 4) == 0) {
                    default_monitor = 0;
                }
                break;
            case QEMU_OPTION_watchdog:
                if (watchdog) {
                    error_report("only one watchdog option may be given");
                    return 1;
                }
                watchdog = optarg;
                break;
            case QEMU_OPTION_watchdog_action:
                if (select_watchdog_action(optarg) == -1) {
                    error_report("unknown -watchdog-action parameter");
                    exit(1);
                }
                break;
            case QEMU_OPTION_virtiocon:
                add_device_config(DEV_VIRTCON, optarg);
                default_virtcon = 0;
                if (strncmp(optarg, "mon:", 4) == 0) {
                    default_monitor = 0;
                }
                break;
            case QEMU_OPTION_parallel:
                add_device_config(DEV_PARALLEL, optarg);
                default_parallel = 0;
                if (strncmp(optarg, "mon:", 4) == 0) {
                    default_monitor = 0;
                }
                break;
            case QEMU_OPTION_debugcon:
                add_device_config(DEV_DEBUGCON, optarg);
                break;
            case QEMU_OPTION_loadvm:
                loadvm = optarg;
                break;
            case QEMU_OPTION_full_screen:
                full_screen = 1;
                break;
            case QEMU_OPTION_no_frame:
                no_frame = 1;
                break;
            case QEMU_OPTION_alt_grab:
                alt_grab = 1;
                break;
            case QEMU_OPTION_ctrl_grab:
                ctrl_grab = 1;
                break;
            case QEMU_OPTION_no_quit:
                no_quit = 1;
                break;
            case QEMU_OPTION_sdl:
#ifdef CONFIG_SDL
                display_type = DT_SDL;
                break;
#else
                error_report("SDL support is disabled");
                exit(1);
#endif
            case QEMU_OPTION_pidfile:
                pid_file = optarg;
                break;
            case QEMU_OPTION_win2k_hack:
                win2k_install_hack = 1;
                break;
            case QEMU_OPTION_rtc_td_hack: {
                static GlobalProperty slew_lost_ticks = {
                    .driver   = "mc146818rtc",
                    .property = "lost_tick_policy",
                    .value    = "slew",
                };

                qdev_prop_register_global(&slew_lost_ticks);
                break;
            }
            case QEMU_OPTION_acpitable:
                opts = qemu_opts_parse_noisily(qemu_find_opts("acpi"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                do_acpitable_option(opts);
                break;
            case QEMU_OPTION_smbios:
                opts = qemu_opts_parse_noisily(qemu_find_opts("smbios"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                do_smbios_option(opts);
                break;
            case QEMU_OPTION_fwcfg:
                opts = qemu_opts_parse_noisily(qemu_find_opts("fw_cfg"),
                                               optarg, true);
                if (opts == NULL) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_enable_kvm:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "accel=kvm", false);
                break;
            case QEMU_OPTION_M:
            case QEMU_OPTION_machine:
                olist = qemu_find_opts("machine");
                opts = qemu_opts_parse_noisily(olist, optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
             case QEMU_OPTION_no_kvm:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "accel=tcg", false);
                break;
            case QEMU_OPTION_no_kvm_pit: {
                error_report("warning: ignoring deprecated option");
                break;
            }
            case QEMU_OPTION_no_kvm_pit_reinjection: {
                static GlobalProperty kvm_pit_lost_tick_policy = {
                    .driver   = "kvm-pit",
                    .property = "lost_tick_policy",
                    .value    = "discard",
                };

                error_report("warning: deprecated, replaced by "
                             "-global kvm-pit.lost_tick_policy=discard");
                qdev_prop_register_global(&kvm_pit_lost_tick_policy);
                break;
            }
            case QEMU_OPTION_usb:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "usb=on", false);
                break;
            case QEMU_OPTION_usbdevice:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "usb=on", false);
                add_device_config(DEV_USB, optarg);
                break;
            case QEMU_OPTION_device:
                if (!qemu_opts_parse_noisily(qemu_find_opts("device"),
                                             optarg, true)) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_smp:
                if (!qemu_opts_parse_noisily(qemu_find_opts("smp-opts"),
                                             optarg, true)) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_vnc:
                vnc_parse(optarg, &error_fatal);
                break;
            case QEMU_OPTION_no_acpi:
                acpi_enabled = 0;
                break;
            case QEMU_OPTION_no_hpet:
                no_hpet = 1;
                break;
            case QEMU_OPTION_balloon:
                if (balloon_parse(optarg) < 0) {
                    error_report("unknown -balloon argument %s", optarg);
                    exit(1);
                }
                break;
            case QEMU_OPTION_no_reboot:
                no_reboot = 1;
                break;
            case QEMU_OPTION_no_shutdown:
                no_shutdown = 1;
                break;
            case QEMU_OPTION_show_cursor:
                cursor_hide = 0;
                break;
            case QEMU_OPTION_uuid:
                if(qemu_uuid_parse(optarg, qemu_uuid) < 0) {
                    error_report("failed to parse UUID string: wrong format");
                    exit(1);
                }
                qemu_uuid_set = true;
                break;
            case QEMU_OPTION_option_rom:
                if (nb_option_roms >= MAX_OPTION_ROMS) {
                    error_report("too many option ROMs");
                    exit(1);
                }
                opts = qemu_opts_parse_noisily(qemu_find_opts("option-rom"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                option_rom[nb_option_roms].name = qemu_opt_get(opts, "romfile");
                option_rom[nb_option_roms].bootindex =
                    qemu_opt_get_number(opts, "bootindex", -1);
                if (!option_rom[nb_option_roms].name) {
                    error_report("Option ROM file is not specified");
                    exit(1);
                }
                nb_option_roms++;
                break;
            case QEMU_OPTION_semihosting:
                semihosting.enabled = true;
                semihosting.target = SEMIHOSTING_TARGET_AUTO;
                break;
            case QEMU_OPTION_semihosting_config:
                semihosting.enabled = true;
                opts = qemu_opts_parse_noisily(qemu_find_opts("semihosting-config"),
                                               optarg, false);
                if (opts != NULL) {
                    semihosting.enabled = qemu_opt_get_bool(opts, "enable",
                                                            true);
                    const char *target = qemu_opt_get(opts, "target");
                    if (target != NULL) {
                        if (strcmp("native", target) == 0) {
                            semihosting.target = SEMIHOSTING_TARGET_NATIVE;
                        } else if (strcmp("gdb", target) == 0) {
                            semihosting.target = SEMIHOSTING_TARGET_GDB;
                        } else  if (strcmp("auto", target) == 0) {
                            semihosting.target = SEMIHOSTING_TARGET_AUTO;
                        } else {
                            error_report("unsupported semihosting-config %s",
                                         optarg);
                            exit(1);
                        }
                    } else {
                        semihosting.target = SEMIHOSTING_TARGET_AUTO;
                    }
                    /* Set semihosting argument count and vector */
                    qemu_opt_foreach(opts, add_semihosting_arg,
                                     &semihosting, NULL);
                } else {
                    error_report("unsupported semihosting-config %s", optarg);
                    exit(1);
                }
                break;
            case QEMU_OPTION_tdf:
                error_report("warning: ignoring deprecated option");
                break;
            case QEMU_OPTION_name:
                opts = qemu_opts_parse_noisily(qemu_find_opts("name"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_prom_env:
                if (nb_prom_envs >= MAX_PROM_ENVS) {
                    error_report("too many prom variables");
                    exit(1);
                }
                prom_envs[nb_prom_envs] = optarg;
                nb_prom_envs++;
                break;
            case QEMU_OPTION_old_param:
                old_param = 1;
                break;
            case QEMU_OPTION_clock:
                /* Clock options no longer exist.  Keep this option for
                 * backward compatibility.
                 */
                break;
            case QEMU_OPTION_startdate:
                configure_rtc_date_offset(optarg, 1);
                break;
            case QEMU_OPTION_rtc:
                opts = qemu_opts_parse_noisily(qemu_find_opts("rtc"), optarg,
                                               false);
                if (!opts) {
                    exit(1);
                }
                configure_rtc(opts);
                break;
            case QEMU_OPTION_tb_size:
                tcg_tb_size = strtol(optarg, NULL, 0);
                if (tcg_tb_size < 0) {
                    tcg_tb_size = 0;
                }
                break;
            case QEMU_OPTION_icount:
                icount_opts = qemu_opts_parse_noisily(qemu_find_opts("icount"),
                                                      optarg, true);
                if (!icount_opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_incoming:
                if (!incoming) {
                    runstate_set(RUN_STATE_INMIGRATE);
                }
                incoming = optarg;
                break;
            case QEMU_OPTION_nodefaults:
                has_defaults = 0;
                break;
            case QEMU_OPTION_xen_domid:
                if (!(xen_available())) {
                    printf("Option %s not supported for this target\n", popt->name);
                    exit(1);
                }
                xen_domid = atoi(optarg);
                break;
            case QEMU_OPTION_xen_create:
                if (!(xen_available())) {
                    printf("Option %s not supported for this target\n", popt->name);
                    exit(1);
                }
                xen_mode = XEN_CREATE;
                break;
            case QEMU_OPTION_xen_attach:
                if (!(xen_available())) {
                    printf("Option %s not supported for this target\n", popt->name);
                    exit(1);
                }
                xen_mode = XEN_ATTACH;
                break;
            case QEMU_OPTION_trace:
            {
                opts = qemu_opts_parse_noisily(qemu_find_opts("trace"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                if (qemu_opt_get(opts, "enable")) {
                    trace_enable_events(qemu_opt_get(opts, "enable"));
                }
                trace_init_events(qemu_opt_get(opts, "events"));
                if (trace_file) {
                    g_free(trace_file);
                }
                trace_file = g_strdup(qemu_opt_get(opts, "file"));
                qemu_opts_del(opts);
                break;
            }
            case QEMU_OPTION_readconfig:
                {
                    int ret = qemu_read_config_file(optarg);
                    if (ret < 0) {
                        error_report("read config %s: %s", optarg,
                                     strerror(-ret));
                        exit(1);
                    }
                    break;
                }
            case QEMU_OPTION_spice:
                olist = qemu_find_opts("spice");
                if (!olist) {
                    error_report("spice support is disabled");
                    exit(1);
                }
                opts = qemu_opts_parse_noisily(olist, optarg, false);
                if (!opts) {
                    exit(1);
                }
                display_remote++;
                break;
            case QEMU_OPTION_writeconfig:
                {
                    FILE *fp;
                    if (strcmp(optarg, "-") == 0) {
                        fp = stdout;
                    } else {
                        fp = fopen(optarg, "w");
                        if (fp == NULL) {
                            error_report("open %s: %s", optarg,
                                         strerror(errno));
                            exit(1);
                        }
                    }
                    qemu_config_write(fp);
                    if (fp != stdout) {
                        fclose(fp);
                    }
                    break;
                }
            case QEMU_OPTION_qtest:
                qtest_chrdev = optarg;
                break;
            case QEMU_OPTION_qtest_log:
                qtest_log = optarg;
                break;
            case QEMU_OPTION_sandbox:
                opts = qemu_opts_parse_noisily(qemu_find_opts("sandbox"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_add_fd:
#ifndef _WIN32
                opts = qemu_opts_parse_noisily(qemu_find_opts("add-fd"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
#else
                error_report("File descriptor passing is disabled on this "
                             "platform");
                exit(1);
#endif
                break;
            case QEMU_OPTION_object:
                opts = qemu_opts_parse_noisily(qemu_find_opts("object"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_realtime:
                opts = qemu_opts_parse_noisily(qemu_find_opts("realtime"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                enable_mlock = qemu_opt_get_bool(opts, "mlock", true);
                break;
            case QEMU_OPTION_msg:
                opts = qemu_opts_parse_noisily(qemu_find_opts("msg"), optarg,
                                               false);
                if (!opts) {
                    exit(1);
                }
                configure_msg(opts);
                break;
            case QEMU_OPTION_dump_vmstate:
                if (vmstate_dump_file) {
                    error_report("only one '-dump-vmstate' "
                                 "option may be given");
                    exit(1);
                }
                vmstate_dump_file = fopen(optarg, "w");
                if (vmstate_dump_file == NULL) {
                    error_report("open %s: %s", optarg, strerror(errno));
                    exit(1);
                }
                break;
            default:
                os_parse_cmd_args(popt->index, optarg);
            }
        }
    }
    /*
     * Clear error location left behind by the loop.
     * Best done right after the loop.  Do not insert code here!
     */
    loc_set_none();

    replay_configure(icount_opts);

    machine_class = select_machine();

    set_memory_options(&ram_slots, &maxram_size, machine_class);

    os_daemonize();

    if (qemu_init_main_loop(&main_loop_err)) {
        error_report_err(main_loop_err);
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("sandbox"),
                          parse_sandbox, NULL, NULL)) {
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("name"),
                          parse_name, NULL, NULL)) {
        exit(1);
    }

#ifndef _WIN32
    if (qemu_opts_foreach(qemu_find_opts("add-fd"),
                          parse_add_fd, NULL, NULL)) {
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("add-fd"),
                          cleanup_add_fd, NULL, NULL)) {
        exit(1);
    }
#endif

    current_machine = MACHINE(object_new(object_class_get_name(
                          OBJECT_CLASS(machine_class))));
    if (machine_help_func(qemu_get_machine_opts(), current_machine)) {
        exit(0);
    }
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine), &error_abort);
    cpu_exec_init_all();

    if (machine_class->hw_version) {
        qemu_set_hw_version(machine_class->hw_version);
    }

    if (cpu_model && is_help_option(cpu_model)) {
        list_cpus(stdout, &fprintf, cpu_model);
        exit(0);
    }

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);

    /* Open the logfile at this point and set the log mask if necessary.
     */
    if (log_file) {
        qemu_set_log_filename(log_file);
    }

    if (log_mask) {
        int mask;
        mask = qemu_str_to_log_mask(log_mask);
        if (!mask) {
            qemu_print_log_usage(stdout);
            exit(1);
        }
        qemu_set_log(mask);
    } else {
        qemu_set_log(0);
    }

    /* If no data_dir is specified then try to find it relative to the
       executable path.  */
    if (data_dir_idx < ARRAY_SIZE(data_dir)) {
        data_dir[data_dir_idx] = os_find_datadir();
        if (data_dir[data_dir_idx] != NULL) {
            data_dir_idx++;
        }
    }
    /* If all else fails use the install path specified when building. */
    if (data_dir_idx < ARRAY_SIZE(data_dir)) {
        data_dir[data_dir_idx++] = CONFIG_QEMU_DATADIR;
    }

    smp_parse(qemu_opts_find(qemu_find_opts("smp-opts"), NULL));

    machine_class->max_cpus = machine_class->max_cpus ?: 1; /* Default to UP */
    if (max_cpus > machine_class->max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine '%s' (%d)", max_cpus,
                     machine_class->name, machine_class->max_cpus);
        exit(1);
    }

    /*
     * Get the default machine options from the machine if it is not already
     * specified either by the configuration file or by the command line.
     */
    if (machine_class->default_machine_opts) {
        qemu_opts_set_defaults(qemu_find_opts("machine"),
                               machine_class->default_machine_opts, 0);
    }

    qemu_opts_foreach(qemu_find_opts("device"),
                      default_driver_check, NULL, NULL);
    qemu_opts_foreach(qemu_find_opts("global"),
                      default_driver_check, NULL, NULL);

    if (!vga_model && !default_vga) {
        vga_interface_type = VGA_DEVICE;
    }
    if (!has_defaults || machine_class->no_serial) {
        default_serial = 0;
    }
    if (!has_defaults || machine_class->no_parallel) {
        default_parallel = 0;
    }
    if (!has_defaults || !machine_class->use_virtcon) {
        default_virtcon = 0;
    }
    if (!has_defaults || !machine_class->use_sclp) {
        default_sclp = 0;
    }
    if (!has_defaults || machine_class->no_floppy) {
        default_floppy = 0;
    }
    if (!has_defaults || machine_class->no_cdrom) {
        default_cdrom = 0;
    }
    if (!has_defaults || machine_class->no_sdcard) {
        default_sdcard = 0;
    }
    if (!has_defaults) {
        default_monitor = 0;
        default_net = 0;
        default_vga = 0;
    }

    if (is_daemonized()) {
        /* According to documentation and historically, -nographic redirects
         * serial port, parallel port and monitor to stdio, which does not work
         * with -daemonize.  We can redirect these to null instead, but since
         * -nographic is legacy, let's just error out.
         * We disallow -nographic only if all other ports are not redirected
         * explicitly, to not break existing legacy setups which uses
         * -nographic _and_ redirects all ports explicitly - this is valid
         * usage, -nographic is just a no-op in this case.
         */
        if (nographic
            && (default_parallel || default_serial
                || default_monitor || default_virtcon)) {
            error_report("-nographic cannot be used with -daemonize");
            exit(1);
        }
#ifdef CONFIG_CURSES
        if (display_type == DT_CURSES) {
            error_report("curses display cannot be used with -daemonize");
            exit(1);
        }
#endif
    }

    if (nographic) {
        if (default_parallel)
            add_device_config(DEV_PARALLEL, "null");
        if (default_serial && default_monitor) {
            add_device_config(DEV_SERIAL, "mon:stdio");
        } else if (default_virtcon && default_monitor) {
            add_device_config(DEV_VIRTCON, "mon:stdio");
        } else if (default_sclp && default_monitor) {
            add_device_config(DEV_SCLP, "mon:stdio");
        } else {
            if (default_serial)
                add_device_config(DEV_SERIAL, "stdio");
            if (default_virtcon)
                add_device_config(DEV_VIRTCON, "stdio");
            if (default_sclp) {
                add_device_config(DEV_SCLP, "stdio");
            }
            if (default_monitor)
                monitor_parse("stdio", "readline", false);
        }
    } else {
        if (default_serial)
            add_device_config(DEV_SERIAL, "vc:80Cx24C");
        if (default_parallel)
            add_device_config(DEV_PARALLEL, "vc:80Cx24C");
        if (default_monitor)
            monitor_parse("vc:80Cx24C", "readline", false);
        if (default_virtcon)
            add_device_config(DEV_VIRTCON, "vc:80Cx24C");
        if (default_sclp) {
            add_device_config(DEV_SCLP, "vc:80Cx24C");
        }
    }

#if defined(CONFIG_VNC)
    if (!QTAILQ_EMPTY(&(qemu_find_opts("vnc")->head))) {
        display_remote++;
    }
#endif
    if (display_type == DT_DEFAULT && !display_remote) {
#if defined(CONFIG_GTK)
        display_type = DT_GTK;
#elif defined(CONFIG_SDL)
        display_type = DT_SDL;
#elif defined(CONFIG_COCOA)
        display_type = DT_COCOA;
#elif defined(CONFIG_VNC)
        vnc_parse("localhost:0,to=99,id=default", &error_abort);
        show_vnc_port = 1;
#else
        display_type = DT_NONE;
#endif
    }

    if ((no_frame || alt_grab || ctrl_grab) && display_type != DT_SDL) {
        error_report("-no-frame, -alt-grab and -ctrl-grab are only valid "
                     "for SDL, ignoring option");
    }
    if (no_quit && (display_type != DT_GTK && display_type != DT_SDL)) {
        error_report("-no-quit is only valid for GTK and SDL, "
                     "ignoring option");
    }

    if (display_type == DT_GTK) {
        early_gtk_display_init(request_opengl);
    }

    if (display_type == DT_SDL) {
        sdl_display_early_init(request_opengl);
    }

    if (request_opengl == 1 && display_opengl == 0) {
#if defined(CONFIG_OPENGL)
        error_report("OpenGL is not supported by the display");
#else
        error_report("OpenGL support is disabled");
#endif
        exit(1);
    }

    page_size_init();
    socket_init();

    if (qemu_opts_foreach(qemu_find_opts("object"),
                          user_creatable_add_opts_foreach,
                          object_create_initial, NULL)) {
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("chardev"),
                          chardev_init_func, NULL, NULL)) {
        exit(1);
    }

#ifdef CONFIG_VIRTFS
    if (qemu_opts_foreach(qemu_find_opts("fsdev"),
                          fsdev_init_func, NULL, NULL)) {
        exit(1);
    }
#endif

    if (pid_file && qemu_create_pidfile(pid_file) != 0) {
        error_report("could not acquire pid file: %s", strerror(errno));
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("device"),
                          device_help_func, NULL, NULL)) {
        exit(0);
    }

    machine_opts = qemu_get_machine_opts();
    if (qemu_opt_foreach(machine_opts, machine_set_property, current_machine,
                         NULL)) {
        object_unref(OBJECT(current_machine));
        exit(1);
    }

    configure_accelerator(current_machine);

    if (qtest_chrdev) {
        qtest_init(qtest_chrdev, qtest_log, &error_fatal);
    }

    machine_opts = qemu_get_machine_opts();
    kernel_filename = qemu_opt_get(machine_opts, "kernel");
    initrd_filename = qemu_opt_get(machine_opts, "initrd");
    kernel_cmdline = qemu_opt_get(machine_opts, "append");
    bios_name = qemu_opt_get(machine_opts, "firmware");

    opts = qemu_opts_find(qemu_find_opts("boot-opts"), NULL);
    if (opts) {
        boot_order = qemu_opt_get(opts, "order");
        if (boot_order) {
            validate_bootdevices(boot_order, &error_fatal);
        }

        boot_once = qemu_opt_get(opts, "once");
        if (boot_once) {
            validate_bootdevices(boot_once, &error_fatal);
        }

        boot_menu = qemu_opt_get_bool(opts, "menu", boot_menu);
        boot_strict = qemu_opt_get_bool(opts, "strict", false);
    }

    if (!boot_order) {
        boot_order = machine_class->default_boot_order;
    }

    if (!kernel_cmdline) {
        kernel_cmdline = "";
        current_machine->kernel_cmdline = (char *)kernel_cmdline;
    }

    linux_boot = (kernel_filename != NULL);

    if (!linux_boot && *kernel_cmdline != '\0') {
        error_report("-append only allowed with -kernel option");
        exit(1);
    }

    if (!linux_boot && initrd_filename != NULL) {
        error_report("-initrd only allowed with -kernel option");
        exit(1);
    }

    if (!linux_boot && qemu_opt_get(machine_opts, "dtb")) {
        error_report("-dtb only allowed with -kernel option");
        exit(1);
    }

    if (semihosting_enabled() && !semihosting_get_argc() && kernel_filename) {
        /* fall back to the -kernel/-append */
        semihosting_arg_fallback(kernel_filename, kernel_cmdline);
    }

    os_set_line_buffering();

    /* spice needs the timers to be initialized by this point */
    qemu_spice_init();

    cpu_ticks_init();
    if (icount_opts) {
        if (kvm_enabled() || xen_enabled()) {
            error_report("-icount is not allowed with kvm or xen");
            exit(1);
        }
        configure_icount(icount_opts, &error_abort);
        qemu_opts_del(icount_opts);
    }

    /* clean up network at qemu process termination */
    atexit(&net_cleanup);

    if (default_net) {
        QemuOptsList *net = qemu_find_opts("net");
        qemu_opts_set(net, NULL, "type", "nic", &error_abort);
#ifdef CONFIG_SLIRP
        qemu_opts_set(net, NULL, "type", "user", &error_abort);
#endif
    }

    if (net_init_clients() < 0) {
        exit(1);
    }

    if (qemu_opts_foreach(qemu_find_opts("object"),
                          user_creatable_add_opts_foreach,
                          object_create_delayed, NULL)) {
        exit(1);
    }

#ifdef CONFIG_TPM
    if (tpm_init() < 0) {
        exit(1);
    }
#endif

    /* init the bluetooth world */
    if (foreach_device_config(DEV_BT, bt_parse))
        exit(1);

    if (!xen_enabled()) {
        /* On 32-bit hosts, QEMU is limited by virtual address space */
        if (ram_size > (2047 << 20) && HOST_LONG_BITS == 32) {
            error_report("at most 2047 MB RAM can be simulated");
            exit(1);
        }
    }

    blk_mig_init();
    ram_mig_init();

    /* If the currently selected machine wishes to override the units-per-bus
     * property of its default HBA interface type, do so now. */
    if (machine_class->units_per_default_bus) {
        override_max_devs(machine_class->block_default_type,
                          machine_class->units_per_default_bus);
    }

    /* open the virtual block devices */
    if (snapshot || replay_mode != REPLAY_MODE_NONE) {
        qemu_opts_foreach(qemu_find_opts("drive"), drive_enable_snapshot,
                          NULL, NULL);
    }
    if (qemu_opts_foreach(qemu_find_opts("drive"), drive_init_func,
                          &machine_class->block_default_type, NULL)) {
        exit(1);
    }

    default_drive(default_cdrom, snapshot, machine_class->block_default_type, 2,
                  CDROM_OPTS);
    default_drive(default_floppy, snapshot, IF_FLOPPY, 0, FD_OPTS);
    default_drive(default_sdcard, snapshot, IF_SD, 0, SD_OPTS);

    parse_numa_opts(machine_class);

    if (qemu_opts_foreach(qemu_find_opts("mon"),
                          mon_init_func, NULL, NULL)) {
        exit(1);
    }

    if (foreach_device_config(DEV_SERIAL, serial_parse) < 0)
        exit(1);
    if (foreach_device_config(DEV_PARALLEL, parallel_parse) < 0)
        exit(1);
    if (foreach_device_config(DEV_VIRTCON, virtcon_parse) < 0)
        exit(1);
    if (foreach_device_config(DEV_SCLP, sclp_parse) < 0) {
        exit(1);
    }
    if (foreach_device_config(DEV_DEBUGCON, debugcon_parse) < 0)
        exit(1);

    /* If no default VGA is requested, the default is "none".  */
    if (default_vga) {
        if (machine_class->default_display) {
            vga_model = machine_class->default_display;
        } else if (vga_interface_available(VGA_CIRRUS)) {
            vga_model = "cirrus";
        } else if (vga_interface_available(VGA_STD)) {
            vga_model = "std";
        }
    }
    if (vga_model) {
        select_vgahw(vga_model);
    }

    if (watchdog) {
        i = select_watchdog(watchdog);
        if (i > 0)
            exit (i == 1 ? 1 : 0);
    }

    if (machine_class->compat_props) {
        GlobalProperty *p;
        for (i = 0; i < machine_class->compat_props->len; i++) {
            p = g_array_index(machine_class->compat_props, GlobalProperty *, i);
            qdev_prop_register_global(p);
        }
    }
    qemu_add_globals();

    /* This checkpoint is required by replay to separate prior clock
       reading from the other reads, because timer polling functions query
       clock values from the log. */
    replay_checkpoint(CHECKPOINT_INIT);
    qdev_machine_init();

    current_machine->ram_size = ram_size;
    current_machine->maxram_size = maxram_size;
    current_machine->ram_slots = ram_slots;
    current_machine->boot_order = boot_order;
    current_machine->cpu_model = cpu_model;

    machine_class->init(current_machine);

    realtime_init();

    audio_init();

    cpu_synchronize_all_post_init();

    numa_post_machine_init();

    if (qemu_opts_foreach(qemu_find_opts("fw_cfg"),
                          parse_fw_cfg, fw_cfg_find(), NULL) != 0) {
        exit(1);
    }

    /* init USB devices */
    if (machine_usb(current_machine)) {
        if (foreach_device_config(DEV_USB, usb_parse) < 0)
            exit(1);
    }

    /* Check if IGD GFX passthrough. */
    igd_gfx_passthru();

    /* init generic devices */
    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_DEVICE);
    if (qemu_opts_foreach(qemu_find_opts("device"),
                          device_init_func, NULL, NULL)) {
        exit(1);
    }
    rom_reset_order_override();

    /* Did we create any drives that we failed to create a device for? */
    drive_check_orphaned();

    /* Don't warn about the default network setup that you get if
     * no command line -net or -netdev options are specified. There
     * are two cases that we would otherwise complain about:
     * (1) board doesn't support a NIC but the implicit "-net nic"
     * requested one
     * (2) CONFIG_SLIRP not set, in which case the implicit "-net nic"
     * sets up a nic that isn't connected to anything.
     */
    if (!default_net) {
        net_check_clients();
    }


    if (boot_once) {
        qemu_boot_set(boot_once, &error_fatal);
        qemu_register_reset(restore_boot_order, g_strdup(boot_order));
    }

    ds = init_displaystate();

    /* init local displays */
    switch (display_type) {
    case DT_CURSES:
        curses_display_init(ds, full_screen);
        break;
    case DT_SDL:
        sdl_display_init(ds, full_screen, no_frame);
        break;
    case DT_COCOA:
        cocoa_display_init(ds, full_screen);
        break;
    case DT_GTK:
        gtk_display_init(ds, full_screen, grab_on_hover);
        break;
    default:
        break;
    }

    /* must be after terminal init, SDL library changes signal handlers */
    os_setup_signal_handling();

    /* init remote displays */
    qemu_opts_foreach(qemu_find_opts("vnc"),
                      vnc_init_func, NULL, NULL);
    if (show_vnc_port) {
        char *ret = vnc_display_local_addr("default");
        printf("VNC server running on '%s'\n", ret);
        g_free(ret);
    }

    if (using_spice) {
        qemu_spice_display_init();
    }

    if (foreach_device_config(DEV_GDB, gdbserver_start) < 0) {
        exit(1);
    }

    qdev_machine_creation_done();

    /* TODO: once all bus devices are qdevified, this should be done
     * when bus is created by qdev.c */
    qemu_register_reset(qbus_reset_all_fn, sysbus_get_default());
    qemu_run_machine_init_done_notifiers();

    if (rom_check_and_register_reset() != 0) {
        error_report("rom check and register reset failed");
        exit(1);
    }

    replay_start();

    /* This checkpoint is required by replay to separate prior clock
       reading from the other reads, because timer polling functions query
       clock values from the log. */
    replay_checkpoint(CHECKPOINT_RESET);
    qemu_system_reset(VMRESET_SILENT);
    register_global_state();
    if (loadvm) {
        if (load_vmstate(loadvm) < 0) {
            autostart = 0;
        }
    }

    qdev_prop_check_globals();
    if (vmstate_dump_file) {
        /* dump and exit */
        dump_vmstate_json_to_file(vmstate_dump_file);
        return 0;
    }

    if (incoming) {
        Error *local_err = NULL;
        qemu_start_incoming_migration(incoming, &local_err);
        if (local_err) {
            error_reportf_err(local_err, "-incoming %s: ", incoming);
            exit(1);
        }
    } else if (autostart) {
        vm_start();
    }

    os_setup_post();

    main_loop();
    replay_disable_events();

    bdrv_close_all();
    pause_all_vcpus();
    res_free();
#ifdef CONFIG_TPM
    tpm_cleanup();
#endif

    return 0;
}
