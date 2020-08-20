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
#include "qemu-common.h"
#include "qemu/units.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu-version.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/uuid.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/seccomp.h"
#include "sysemu/tcg.h"
#include "sysemu/xen.h"

#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "sysemu/accel.h"
#include "hw/usb.h"
#include "hw/isa/isa.h"
#include "hw/scsi/scsi.h"
#include "hw/display/vga.h"
#include "sysemu/watchdog.h"
#include "hw/firmware/smbios.h"
#include "hw/acpi/acpi.h"
#include "hw/xen/xen.h"
#include "hw/loader.h"
#include "monitor/qdev.h"
#include "net/net.h"
#include "net/slirp.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/hostmem.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "migration/misc.h"
#include "migration/snapshot.h"
#include "migration/global_state.h"
#include "sysemu/tpm.h"
#include "sysemu/dma.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "sysemu/cpus.h"
#include "migration/colo.h"
#include "migration/postcopy-ram.h"
#include "sysemu/kvm.h"
#include "sysemu/hax.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu-options.h"
#include "qemu/main-loop.h"
#ifdef CONFIG_VIRTFS
#include "fsdev/qemu-fsdev.h"
#endif
#include "sysemu/qtest.h"

#include "disas/disas.h"

#include "trace-root.h"
#include "trace/control.h"
#include "qemu/plugin.h"
#include "qemu/queue.h"
#include "sysemu/arch_init.h"

#include "ui/qemu-spice.h"
#include "qapi/string-input-visitor.h"
#include "qapi/opts-visitor.h"
#include "qapi/clone-visitor.h"
#include "qom/object_interfaces.h"
#include "hw/semihosting/semihost.h"
#include "crypto/init.h"
#include "sysemu/replay.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-visit-ui.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/iothread.h"
#include "qemu/guest-random.h"

#define MAX_VIRTIO_CONSOLES 1

static const char *data_dir[16];
static int data_dir_idx;
const char *bios_name = NULL;
enum vga_retrace_method vga_retrace_method = VGA_RETRACE_DUMB;
int display_opengl;
const char* keyboard_layout = NULL;
ram_addr_t ram_size;
bool enable_mlock = false;
bool enable_cpu_pm = false;
int nb_nics;
NICInfo nd_table[MAX_NICS];
int autostart;
static enum {
    RTC_BASE_UTC,
    RTC_BASE_LOCALTIME,
    RTC_BASE_DATETIME,
} rtc_base_type = RTC_BASE_UTC;
static time_t rtc_ref_start_datetime;
static int rtc_realtime_clock_offset; /* used only with QEMU_CLOCK_REALTIME */
static int rtc_host_datetime_offset = -1; /* valid & used only with
                                             RTC_BASE_DATETIME */
QEMUClockType rtc_clock;
int vga_interface_type = VGA_NONE;
static DisplayOptions dpy;
static int num_serial_hds;
static Chardev **serial_hds;
Chardev *parallel_hds[MAX_PARALLEL_PORTS];
int win2k_install_hack = 0;
int singlestep = 0;
int no_hpet = 0;
int fd_bootchk = 1;
static int no_reboot;
int no_shutdown = 0;
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
int only_migratable; /* turn it off unless user states otherwise */
bool wakeup_suspend_enabled;

int icount_align_option;

/* The bytes in qemu_uuid are in the order specified by RFC4122, _not_ in the
 * little-endian "wire format" described in the SMBIOS 2.6 specification.
 */
QemuUUID qemu_uuid;
bool qemu_uuid_set;

static NotifierList exit_notifiers =
    NOTIFIER_LIST_INITIALIZER(exit_notifiers);

static NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);

uint32_t xen_domid;
enum xen_mode xen_mode = XEN_EMULATE;
bool xen_domid_restrict;

static int has_defaults = 1;
static int default_serial = 1;
static int default_parallel = 1;
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
    { .driver = "floppy",               .flag = &default_floppy    },
    { .driver = "ide-cd",               .flag = &default_cdrom     },
    { .driver = "ide-hd",               .flag = &default_cdrom     },
    { .driver = "ide-drive",            .flag = &default_cdrom     },
    { .driver = "scsi-cd",              .flag = &default_cdrom     },
    { .driver = "scsi-hd",              .flag = &default_cdrom     },
    { .driver = "VGA",                  .flag = &default_vga       },
    { .driver = "isa-vga",              .flag = &default_vga       },
    { .driver = "cirrus-vga",           .flag = &default_vga       },
    { .driver = "isa-cirrus-vga",       .flag = &default_vga       },
    { .driver = "vmware-svga",          .flag = &default_vga       },
    { .driver = "qxl-vga",              .flag = &default_vga       },
    { .driver = "virtio-vga",           .flag = &default_vga       },
    { .driver = "ati-vga",              .flag = &default_vga       },
    { .driver = "vhost-user-vga",       .flag = &default_vga       },
};

static QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .merge_lists = true,
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

static QemuOptsList qemu_accel_opts = {
    .name = "accel",
    .implied_opt_name = "accel",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_accel_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting accelerator properties
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
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "reboot-timeout",
            .type = QEMU_OPT_NUMBER,
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

static QemuOptsList qemu_overcommit_opts = {
    .name = "overcommit",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_overcommit_opts.head),
    .desc = {
        {
            .name = "mem-lock",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "cpu-pm",
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
        {
            .name = "guest-name",
            .type = QEMU_OPT_BOOL,
            .help = "Prepends guest name for error messages but only if "
                    "-name guest is set otherwise option is ignored\n",
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
        }, {
            .name = "rrsnapshot",
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
            .help = "Sets the name of the file from which "
                    "the fw_cfg blob will be loaded",
        }, {
            .name = "string",
            .type = QEMU_OPT_STRING,
            .help = "Sets content of the blob to be inserted from a string",
        }, {
            .name = "gen_id",
            .type = QEMU_OPT_STRING,
            .help = "Sets id of the object generating the fw_cfg blob "
                    "to be inserted",
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

static RunState current_run_state = RUN_STATE_PRECONFIG;

/* We use RUN_STATE__MAX but any invalid value will do */
static RunState vmstop_requested = RUN_STATE__MAX;
static QemuMutex vmstop_lock;

typedef struct {
    RunState from;
    RunState to;
} RunStateTransition;

static const RunStateTransition runstate_transitions_def[] = {
    /*     from      ->     to      */
    { RUN_STATE_PRECONFIG, RUN_STATE_PRELAUNCH },
      /* Early switch to inmigrate state to allow  -incoming CLI option work
       * as it used to. TODO: delay actual switching to inmigrate state to
       * the point after machine is built and remove this hack.
       */
    { RUN_STATE_PRECONFIG, RUN_STATE_INMIGRATE },

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
    { RUN_STATE_INMIGRATE, RUN_STATE_COLO },

    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_PAUSED },
    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_INTERNAL_ERROR, RUN_STATE_PRELAUNCH },

    { RUN_STATE_IO_ERROR, RUN_STATE_RUNNING },
    { RUN_STATE_IO_ERROR, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_IO_ERROR, RUN_STATE_PRELAUNCH },

    { RUN_STATE_PAUSED, RUN_STATE_RUNNING },
    { RUN_STATE_PAUSED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_PAUSED, RUN_STATE_POSTMIGRATE },
    { RUN_STATE_PAUSED, RUN_STATE_PRELAUNCH },
    { RUN_STATE_PAUSED, RUN_STATE_COLO},

    { RUN_STATE_POSTMIGRATE, RUN_STATE_RUNNING },
    { RUN_STATE_POSTMIGRATE, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_POSTMIGRATE, RUN_STATE_PRELAUNCH },

    { RUN_STATE_PRELAUNCH, RUN_STATE_RUNNING },
    { RUN_STATE_PRELAUNCH, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_PRELAUNCH, RUN_STATE_INMIGRATE },

    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_RUNNING },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_PAUSED },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_POSTMIGRATE },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_PRELAUNCH },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_COLO},

    { RUN_STATE_RESTORE_VM, RUN_STATE_RUNNING },
    { RUN_STATE_RESTORE_VM, RUN_STATE_PRELAUNCH },

    { RUN_STATE_COLO, RUN_STATE_RUNNING },

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
    { RUN_STATE_RUNNING, RUN_STATE_COLO},

    { RUN_STATE_SAVE_VM, RUN_STATE_RUNNING },

    { RUN_STATE_SHUTDOWN, RUN_STATE_PAUSED },
    { RUN_STATE_SHUTDOWN, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SHUTDOWN, RUN_STATE_PRELAUNCH },

    { RUN_STATE_DEBUG, RUN_STATE_SUSPENDED },
    { RUN_STATE_RUNNING, RUN_STATE_SUSPENDED },
    { RUN_STATE_SUSPENDED, RUN_STATE_RUNNING },
    { RUN_STATE_SUSPENDED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SUSPENDED, RUN_STATE_PRELAUNCH },
    { RUN_STATE_SUSPENDED, RUN_STATE_COLO},

    { RUN_STATE_WATCHDOG, RUN_STATE_RUNNING },
    { RUN_STATE_WATCHDOG, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_WATCHDOG, RUN_STATE_PRELAUNCH },
    { RUN_STATE_WATCHDOG, RUN_STATE_COLO},

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
    const char *state = RunState_str(current_run_state);
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

    trace_runstate_set(current_run_state, RunState_str(current_run_state),
                       new_state, RunState_str(new_state));

    if (current_run_state == new_state) {
        return;
    }

    if (!runstate_valid_transitions[current_run_state][new_state]) {
        error_report("invalid runstate transition: '%s' -> '%s'",
                     RunState_str(current_run_state),
                     RunState_str(new_state));
        abort();
    }

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

bool qemu_vmstop_requested(RunState *r)
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

/***********************************************************/
/* RTC reference time/date access */
static time_t qemu_ref_timedate(QEMUClockType clock)
{
    time_t value = qemu_clock_get_ms(clock) / 1000;
    switch (clock) {
    case QEMU_CLOCK_REALTIME:
        value -= rtc_realtime_clock_offset;
        /* fall through */
    case QEMU_CLOCK_VIRTUAL:
        value += rtc_ref_start_datetime;
        break;
    case QEMU_CLOCK_HOST:
        if (rtc_base_type == RTC_BASE_DATETIME) {
            value -= rtc_host_datetime_offset;
        }
        break;
    default:
        assert(0);
    }
    return value;
}

void qemu_get_timedate(struct tm *tm, int offset)
{
    time_t ti = qemu_ref_timedate(rtc_clock);

    ti += offset;

    switch (rtc_base_type) {
    case RTC_BASE_DATETIME:
    case RTC_BASE_UTC:
        gmtime_r(&ti, tm);
        break;
    case RTC_BASE_LOCALTIME:
        localtime_r(&ti, tm);
        break;
    }
}

int qemu_timedate_diff(struct tm *tm)
{
    time_t seconds;

    switch (rtc_base_type) {
    case RTC_BASE_DATETIME:
    case RTC_BASE_UTC:
        seconds = mktimegm(tm);
        break;
    case RTC_BASE_LOCALTIME:
    {
        struct tm tmp = *tm;
        tmp.tm_isdst = -1; /* use timezone to figure it out */
        seconds = mktime(&tmp);
        break;
    }
    default:
        abort();
    }

    return seconds - qemu_ref_timedate(QEMU_CLOCK_HOST);
}

static void configure_rtc_base_datetime(const char *startdate)
{
    time_t rtc_start_datetime;
    struct tm tm;

    if (sscanf(startdate, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        /* OK */
    } else if (sscanf(startdate, "%d-%d-%d",
                      &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
    } else {
        goto date_fail;
    }
    tm.tm_year -= 1900;
    tm.tm_mon--;
    rtc_start_datetime = mktimegm(&tm);
    if (rtc_start_datetime == -1) {
    date_fail:
        error_report("invalid datetime format");
        error_printf("valid formats: "
                     "'2006-06-17T16:01:21' or '2006-06-17'\n");
        exit(1);
    }
    rtc_host_datetime_offset = rtc_ref_start_datetime - rtc_start_datetime;
    rtc_ref_start_datetime = rtc_start_datetime;
}

static void configure_rtc(QemuOpts *opts)
{
    const char *value;

    /* Set defaults */
    rtc_clock = QEMU_CLOCK_HOST;
    rtc_ref_start_datetime = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
    rtc_realtime_clock_offset = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000;

    value = qemu_opt_get(opts, "base");
    if (value) {
        if (!strcmp(value, "utc")) {
            rtc_base_type = RTC_BASE_UTC;
        } else if (!strcmp(value, "localtime")) {
            Error *blocker = NULL;
            rtc_base_type = RTC_BASE_LOCALTIME;
            error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED,
                      "-rtc base=localtime");
            replay_add_blocker(blocker);
        } else {
            rtc_base_type = RTC_BASE_DATETIME;
            configure_rtc_base_datetime(value);
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
            object_register_sugar_prop("mc146818rtc",
                                       "lost_tick_policy",
                                       "slew");
        } else if (!strcmp(value, "none")) {
            /* discard is default */
        } else {
            error_report("invalid option value '%s'", value);
            exit(1);
        }
    }
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
        error_setg(errp, "fd option is required and must be non-negative");
        return -1;
    }

    if (fd <= STDERR_FILENO) {
        error_setg(errp, "fd cannot be a standard I/O stream");
        return -1;
    }

    /*
     * All fds inherited across exec() necessarily have FD_CLOEXEC
     * clear, while qemu sets FD_CLOEXEC on all other fds used internally.
     */
    flags = fcntl(fd, F_GETFD);
    if (flags == -1 || (flags & FD_CLOEXEC)) {
        error_setg(errp, "fd is not valid or already in use");
        return -1;
    }

    if (fdset_id < 0) {
        error_setg(errp, "set option is required and must be non-negative");
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
        error_setg(errp, "error duplicating fd: %s", strerror(errno));
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

    return drive_new(opts, *block_default_type, errp) == NULL;
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

    dinfo = drive_new(opts, type, &error_abort);
    dinfo->is_default = true;

}

typedef struct BlockdevOptionsQueueEntry {
    BlockdevOptions *bdo;
    Location loc;
    QSIMPLEQ_ENTRY(BlockdevOptionsQueueEntry) entry;
} BlockdevOptionsQueueEntry;

typedef QSIMPLEQ_HEAD(, BlockdevOptionsQueueEntry) BlockdevOptionsQueue;

static void configure_blockdev(BlockdevOptionsQueue *bdo_queue,
                               MachineClass *machine_class, int snapshot)
{
    /*
     * If the currently selected machine wishes to override the
     * units-per-bus property of its default HBA interface type, do so
     * now.
     */
    if (machine_class->units_per_default_bus) {
        override_max_devs(machine_class->block_default_type,
                          machine_class->units_per_default_bus);
    }

    /* open the virtual block devices */
    while (!QSIMPLEQ_EMPTY(bdo_queue)) {
        BlockdevOptionsQueueEntry *bdo = QSIMPLEQ_FIRST(bdo_queue);

        QSIMPLEQ_REMOVE_HEAD(bdo_queue, entry);
        loc_push_restore(&bdo->loc);
        qmp_blockdev_add(bdo->bdo, &error_fatal);
        loc_pop(&bdo->loc);
        qapi_free_BlockdevOptions(bdo->bdo);
        g_free(bdo);
    }
    if (snapshot) {
        qemu_opts_foreach(qemu_find_opts("drive"), drive_enable_snapshot,
                          NULL, NULL);
    }
    if (qemu_opts_foreach(qemu_find_opts("drive"), drive_init_func,
                          &machine_class->block_default_type, &error_fatal)) {
        /* We printed help */
        exit(0);
    }

    default_drive(default_cdrom, snapshot, machine_class->block_default_type, 2,
                  CDROM_OPTS);
    default_drive(default_floppy, snapshot, IF_FLOPPY, 0, FD_OPTS);
    default_drive(default_sdcard, snapshot, IF_SD, 0, SD_OPTS);

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
            .name = "dies",
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
    error_with_timestamp = qemu_opt_get_bool(opts, "timestamp", false);
    error_with_guestname = qemu_opt_get_bool(opts, "guest-name", false);
}


/***********************************************************/
/* USB devices */

static int usb_device_add(const char *devname)
{
    USBDevice *dev = NULL;

    if (!machine_usb(current_machine)) {
        return -1;
    }

    dev = usbdevice_create(devname);
    if (!dev)
        return -1;

    return 0;
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

/***********************************************************/
/* machine registration */

MachineState *current_machine;

static MachineClass *find_machine(const char *name, GSList *machines)
{
    GSList *el;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (!strcmp(mc->name, name) || !g_strcmp0(mc->alias, name)) {
            return mc;
        }
    }

    return NULL;
}

static MachineClass *find_default_machine(GSList *machines)
{
    GSList *el;
    MachineClass *default_machineclass = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (mc->is_default) {
            assert(default_machineclass == NULL && "Multiple default machines");
            default_machineclass = mc;
        }
    }

    return default_machineclass;
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

        printf("%s.%s=%s", MACHINE_GET_CLASS(machine)->name,
               prop->name, prop->type);
        if (prop->description) {
            printf(" (%s)\n", prop->description);
        } else {
            printf("\n");
        }
    }

    return 1;
}

struct VMChangeStateEntry {
    VMChangeStateHandler *cb;
    void *opaque;
    QTAILQ_ENTRY(VMChangeStateEntry) entries;
    int priority;
};

static QTAILQ_HEAD(, VMChangeStateEntry) vm_change_state_head;

/**
 * qemu_add_vm_change_state_handler_prio:
 * @cb: the callback to invoke
 * @opaque: user data passed to the callback
 * @priority: low priorities execute first when the vm runs and the reverse is
 *            true when the vm stops
 *
 * Register a callback function that is invoked when the vm starts or stops
 * running.
 *
 * Returns: an entry to be freed using qemu_del_vm_change_state_handler()
 */
VMChangeStateEntry *qemu_add_vm_change_state_handler_prio(
        VMChangeStateHandler *cb, void *opaque, int priority)
{
    VMChangeStateEntry *e;
    VMChangeStateEntry *other;

    e = g_malloc0(sizeof(*e));
    e->cb = cb;
    e->opaque = opaque;
    e->priority = priority;

    /* Keep list sorted in ascending priority order */
    QTAILQ_FOREACH(other, &vm_change_state_head, entries) {
        if (priority < other->priority) {
            QTAILQ_INSERT_BEFORE(other, e, entries);
            return e;
        }
    }

    QTAILQ_INSERT_TAIL(&vm_change_state_head, e, entries);
    return e;
}

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque)
{
    return qemu_add_vm_change_state_handler_prio(cb, opaque, 0);
}

void qemu_del_vm_change_state_handler(VMChangeStateEntry *e)
{
    QTAILQ_REMOVE(&vm_change_state_head, e, entries);
    g_free(e);
}

void vm_state_notify(int running, RunState state)
{
    VMChangeStateEntry *e, *next;

    trace_vm_state_notify(running, state, RunState_str(state));

    if (running) {
        QTAILQ_FOREACH_SAFE(e, &vm_change_state_head, entries, next) {
            e->cb(e->opaque, running, state);
        }
    } else {
        QTAILQ_FOREACH_REVERSE_SAFE(e, &vm_change_state_head, entries, next) {
            e->cb(e->opaque, running, state);
        }
    }
}

static ShutdownCause reset_requested;
static ShutdownCause shutdown_requested;
static int shutdown_signal;
static pid_t shutdown_pid;
static int powerdown_requested;
static int debug_requested;
static int suspend_requested;
static bool preconfig_exit_requested = true;
static WakeupReason wakeup_reason;
static NotifierList powerdown_notifiers =
    NOTIFIER_LIST_INITIALIZER(powerdown_notifiers);
static NotifierList suspend_notifiers =
    NOTIFIER_LIST_INITIALIZER(suspend_notifiers);
static NotifierList wakeup_notifiers =
    NOTIFIER_LIST_INITIALIZER(wakeup_notifiers);
static NotifierList shutdown_notifiers =
    NOTIFIER_LIST_INITIALIZER(shutdown_notifiers);
static uint32_t wakeup_reason_mask = ~(1 << QEMU_WAKEUP_REASON_NONE);

ShutdownCause qemu_shutdown_requested_get(void)
{
    return shutdown_requested;
}

ShutdownCause qemu_reset_requested_get(void)
{
    return reset_requested;
}

static int qemu_shutdown_requested(void)
{
    return atomic_xchg(&shutdown_requested, SHUTDOWN_CAUSE_NONE);
}

static void qemu_kill_report(void)
{
    if (!qtest_driver() && shutdown_signal) {
        if (shutdown_pid == 0) {
            /* This happens for eg ^C at the terminal, so it's worth
             * avoiding printing an odd message in that case.
             */
            error_report("terminating on signal %d", shutdown_signal);
        } else {
            char *shutdown_cmd = qemu_get_pid_name(shutdown_pid);

            error_report("terminating on signal %d from pid " FMT_pid " (%s)",
                         shutdown_signal, shutdown_pid,
                         shutdown_cmd ? shutdown_cmd : "<unknown process>");
            g_free(shutdown_cmd);
        }
        shutdown_signal = 0;
    }
}

static ShutdownCause qemu_reset_requested(void)
{
    ShutdownCause r = reset_requested;

    if (r && replay_checkpoint(CHECKPOINT_RESET_REQUESTED)) {
        reset_requested = SHUTDOWN_CAUSE_NONE;
        return r;
    }
    return SHUTDOWN_CAUSE_NONE;
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

void qemu_exit_preconfig_request(void)
{
    preconfig_exit_requested = true;
}

/*
 * Reset the VM. Issue an event unless @reason is SHUTDOWN_CAUSE_NONE.
 */
void qemu_system_reset(ShutdownCause reason)
{
    MachineClass *mc;

    mc = current_machine ? MACHINE_GET_CLASS(current_machine) : NULL;

    cpu_synchronize_all_states();

    if (mc && mc->reset) {
        mc->reset(current_machine);
    } else {
        qemu_devices_reset();
    }
    if (reason && reason != SHUTDOWN_CAUSE_SUBSYSTEM_RESET) {
        qapi_event_send_reset(shutdown_caused_by_guest(reason), reason);
    }
    cpu_synchronize_all_post_reset();
}

/*
 * Wake the VM after suspend.
 */
static void qemu_system_wakeup(void)
{
    MachineClass *mc;

    mc = current_machine ? MACHINE_GET_CLASS(current_machine) : NULL;

    if (mc && mc->wakeup) {
        mc->wakeup(current_machine);
    }
}

void qemu_system_guest_panicked(GuestPanicInformation *info)
{
    qemu_log_mask(LOG_GUEST_ERROR, "Guest crashed");

    if (current_cpu) {
        current_cpu->crash_occurred = true;
    }
    qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_PAUSE,
                                   !!info, info);
    vm_stop(RUN_STATE_GUEST_PANICKED);
    if (!no_shutdown) {
        qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_POWEROFF,
                                       !!info, info);
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_PANIC);
    }

    if (info) {
        if (info->type == GUEST_PANIC_INFORMATION_TYPE_HYPER_V) {
            qemu_log_mask(LOG_GUEST_ERROR, "\nHV crash parameters: (%#"PRIx64
                          " %#"PRIx64" %#"PRIx64" %#"PRIx64" %#"PRIx64")\n",
                          info->u.hyper_v.arg1,
                          info->u.hyper_v.arg2,
                          info->u.hyper_v.arg3,
                          info->u.hyper_v.arg4,
                          info->u.hyper_v.arg5);
        } else if (info->type == GUEST_PANIC_INFORMATION_TYPE_S390) {
            qemu_log_mask(LOG_GUEST_ERROR, " on cpu %d: %s\n"
                          "PSW: 0x%016" PRIx64 " 0x%016" PRIx64"\n",
                          info->u.s390.core,
                          S390CrashReason_str(info->u.s390.reason),
                          info->u.s390.psw_mask,
                          info->u.s390.psw_addr);
        }
        qapi_free_GuestPanicInformation(info);
    }
}

void qemu_system_guest_crashloaded(GuestPanicInformation *info)
{
    qemu_log_mask(LOG_GUEST_ERROR, "Guest crash loaded");

    qapi_event_send_guest_crashloaded(GUEST_PANIC_ACTION_RUN,
                                   !!info, info);

    if (info) {
        qapi_free_GuestPanicInformation(info);
    }
}

void qemu_system_reset_request(ShutdownCause reason)
{
    if (no_reboot && reason != SHUTDOWN_CAUSE_SUBSYSTEM_RESET) {
        shutdown_requested = reason;
    } else {
        reset_requested = reason;
    }
    cpu_stop_current();
    qemu_notify_event();
}

static void qemu_system_suspend(void)
{
    pause_all_vcpus();
    notifier_list_notify(&suspend_notifiers, NULL);
    runstate_set(RUN_STATE_SUSPENDED);
    qapi_event_send_suspend();
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

void qemu_system_wakeup_request(WakeupReason reason, Error **errp)
{
    trace_system_wakeup_request(reason);

    if (!runstate_check(RUN_STATE_SUSPENDED)) {
        error_setg(errp,
                   "Unable to wake up: guest is not in suspended state");
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

void qemu_register_wakeup_support(void)
{
    wakeup_suspend_enabled = true;
}

bool qemu_wakeup_suspend_enabled(void)
{
    return wakeup_suspend_enabled;
}

void qemu_system_killed(int signal, pid_t pid)
{
    shutdown_signal = signal;
    shutdown_pid = pid;
    no_shutdown = 0;

    /* Cannot call qemu_system_shutdown_request directly because
     * we are in a signal handler.
     */
    shutdown_requested = SHUTDOWN_CAUSE_HOST_SIGNAL;
    qemu_notify_event();
}

void qemu_system_shutdown_request(ShutdownCause reason)
{
    trace_qemu_system_shutdown_request(reason);
    replay_shutdown_request(reason);
    shutdown_requested = reason;
    qemu_notify_event();
}

static void qemu_system_powerdown(void)
{
    qapi_event_send_powerdown();
    notifier_list_notify(&powerdown_notifiers, NULL);
}

static void qemu_system_shutdown(ShutdownCause cause)
{
    qapi_event_send_shutdown(shutdown_caused_by_guest(cause), cause);
    notifier_list_notify(&shutdown_notifiers, &cause);
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

void qemu_register_shutdown_notifier(Notifier *notifier)
{
    notifier_list_add(&shutdown_notifiers, notifier);
}

void qemu_system_debug_request(void)
{
    debug_requested = 1;
    qemu_notify_event();
}

static bool main_loop_should_exit(void)
{
    RunState r;
    ShutdownCause request;

    if (preconfig_exit_requested) {
        if (runstate_check(RUN_STATE_PRECONFIG)) {
            runstate_set(RUN_STATE_PRELAUNCH);
        }
        preconfig_exit_requested = false;
        return true;
    }
    if (qemu_debug_requested()) {
        vm_stop(RUN_STATE_DEBUG);
    }
    if (qemu_suspend_requested()) {
        qemu_system_suspend();
    }
    request = qemu_shutdown_requested();
    if (request) {
        qemu_kill_report();
        qemu_system_shutdown(request);
        if (no_shutdown) {
            vm_stop(RUN_STATE_SHUTDOWN);
        } else {
            return true;
        }
    }
    request = qemu_reset_requested();
    if (request) {
        pause_all_vcpus();
        qemu_system_reset(request);
        resume_all_vcpus();
        /*
         * runstate can change in pause_all_vcpus()
         * as iothread mutex is unlocked
         */
        if (!runstate_check(RUN_STATE_RUNNING) &&
                !runstate_check(RUN_STATE_INMIGRATE) &&
                !runstate_check(RUN_STATE_FINISH_MIGRATE)) {
            runstate_set(RUN_STATE_PRELAUNCH);
        }
    }
    if (qemu_wakeup_requested()) {
        pause_all_vcpus();
        qemu_system_wakeup();
        notifier_list_notify(&wakeup_notifiers, &wakeup_reason);
        wakeup_reason = QEMU_WAKEUP_REASON_NONE;
        resume_all_vcpus();
        qapi_event_send_wakeup();
    }
    if (qemu_powerdown_requested()) {
        qemu_system_powerdown();
    }
    if (qemu_vmstop_requested(&r)) {
        vm_stop(r);
    }
    return false;
}

void qemu_main_loop(void)
{
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif
    while (!main_loop_should_exit()) {
#ifdef CONFIG_PROFILER
        ti = profile_getclock();
#endif
        main_loop_wait(false);
#ifdef CONFIG_PROFILER
        dev_time += profile_getclock() - ti;
#endif
    }
}

static void version(void)
{
    printf("QEMU emulator version " QEMU_FULL_VERSION "\n"
           QEMU_COPYRIGHT "\n");
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
           "When using -nographic, press 'ctrl-a h' to get some help.\n"
           "\n"
           QEMU_HELP_BOTTOM "\n");

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

static const VGAInterfaceInfo vga_interfaces[VGA_TYPE_MAX] = {
    [VGA_NONE] = {
        .opt_name = "none",
        .name = "no graphic card",
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
        .name = "Xen paravirtualized framebuffer",
    },
};

static bool vga_interface_available(VGAInterfaceType t)
{
    const VGAInterfaceInfo *ti = &vga_interfaces[t];

    assert(t < VGA_TYPE_MAX);
    return !ti->class_names[0] ||
           module_object_class_by_name(ti->class_names[0]) ||
           module_object_class_by_name(ti->class_names[1]);
}

static const char *
get_default_vga_model(const MachineClass *machine_class)
{
    if (machine_class->default_display) {
        return machine_class->default_display;
    } else if (vga_interface_available(VGA_CIRRUS)) {
        return "cirrus";
    } else if (vga_interface_available(VGA_STD)) {
        return "std";
    }

    return NULL;
}

static void select_vgahw(const MachineClass *machine_class, const char *p)
{
    const char *opts;
    int t;

    if (g_str_equal(p, "help")) {
        const char *def = get_default_vga_model(machine_class);

        for (t = 0; t < VGA_TYPE_MAX; t++) {
            const VGAInterfaceInfo *ti = &vga_interfaces[t];

            if (vga_interface_available(t) && ti->opt_name) {
                printf("%-20s %s%s\n", ti->opt_name, ti->name ?: "",
                       g_str_equal(ti->opt_name, def) ? " (default)" : "");
            }
        }
        exit(0);
    }

    assert(vga_interface_type == VGA_NONE);
    for (t = 0; t < VGA_TYPE_MAX; t++) {
        const VGAInterfaceInfo *ti = &vga_interfaces[t];
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

static void parse_display_qapi(const char *optarg)
{
    DisplayOptions *opts;
    Visitor *v;

    v = qobject_input_visitor_new_str(optarg, "type", &error_fatal);

    visit_type_DisplayOptions(v, NULL, &opts, &error_fatal);
    QAPI_CLONE_MEMBERS(DisplayOptions, &dpy, opts);

    qapi_free_DisplayOptions(opts);
    visit_free(v);
}

DisplayOptions *qmp_query_display_options(Error **errp)
{
    return QAPI_CLONE(DisplayOptions, &dpy);
}

static void parse_display(const char *p)
{
    const char *opts;

    if (is_help_option(p)) {
        qemu_display_help();
        exit(0);
    }

    if (strstart(p, "sdl", &opts)) {
        /*
         * sdl DisplayType needs hand-crafted parser instead of
         * parse_display_qapi() due to some options not in
         * DisplayOptions, specifically:
         *   - frame
         *     Already deprecated.
         *   - ctrl_grab + alt_grab
         *     Not clear yet what happens to them long-term.  Should
         *     replaced by something better or deprecated and dropped.
         */
        dpy.type = DISPLAY_TYPE_SDL;
        while (*opts) {
            const char *nextopt;

            if (strstart(opts, ",alt_grab=", &nextopt)) {
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
                dpy.has_window_close = true;
                if (strstart(opts, "on", &nextopt)) {
                    dpy.window_close = true;
                } else if (strstart(opts, "off", &nextopt)) {
                    dpy.window_close = false;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",show-cursor=", &nextopt)) {
                opts = nextopt;
                dpy.has_show_cursor = true;
                if (strstart(opts, "on", &nextopt)) {
                    dpy.show_cursor = true;
                } else if (strstart(opts, "off", &nextopt)) {
                    dpy.show_cursor = false;
                } else {
                    goto invalid_sdl_args;
                }
            } else if (strstart(opts, ",gl=", &nextopt)) {
                opts = nextopt;
                dpy.has_gl = true;
                if (strstart(opts, "on", &nextopt)) {
                    dpy.gl = DISPLAYGL_MODE_ON;
                } else if (strstart(opts, "core", &nextopt)) {
                    dpy.gl = DISPLAYGL_MODE_CORE;
                } else if (strstart(opts, "es", &nextopt)) {
                    dpy.gl = DISPLAYGL_MODE_ES;
                } else if (strstart(opts, "off", &nextopt)) {
                    dpy.gl = DISPLAYGL_MODE_OFF;
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
    } else if (strstart(p, "vnc", &opts)) {
        /*
         * vnc isn't a (local) DisplayType but a protocol for remote
         * display access.
         */
        if (*opts == '=') {
            vnc_parse(opts + 1, &error_fatal);
        } else {
            error_report("VNC requires a display argument vnc=<display>");
            exit(1);
        }
    } else {
        parse_display_qapi(p);
    }
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

void qemu_add_data_dir(const char *path)
{
    int i;

    if (path == NULL) {
        return;
    }
    if (data_dir_idx == ARRAY_SIZE(data_dir)) {
        return;
    }
    for (i = 0; i < data_dir_idx; i++) {
        if (strcmp(data_dir[i], path) == 0) {
            return; /* duplicate */
        }
    }
    data_dir[data_dir_idx++] = g_strdup(path);
}

static inline bool nonempty_str(const char *str)
{
    return str && *str;
}

static int parse_fw_cfg(void *opaque, QemuOpts *opts, Error **errp)
{
    gchar *buf;
    size_t size;
    const char *name, *file, *str, *gen_id;
    FWCfgState *fw_cfg = (FWCfgState *) opaque;

    if (fw_cfg == NULL) {
        error_setg(errp, "fw_cfg device not available");
        return -1;
    }
    name = qemu_opt_get(opts, "name");
    file = qemu_opt_get(opts, "file");
    str = qemu_opt_get(opts, "string");
    gen_id = qemu_opt_get(opts, "gen_id");

    /* we need the name, and exactly one of: file, content string, gen_id */
    if (!nonempty_str(name) ||
        nonempty_str(file) + nonempty_str(str) + nonempty_str(gen_id) != 1) {
        error_setg(errp, "name, plus exactly one of file,"
                         " string and gen_id, are needed");
        return -1;
    }
    if (strlen(name) > FW_CFG_MAX_FILE_PATH - 1) {
        error_setg(errp, "name too long (max. %d char)",
                   FW_CFG_MAX_FILE_PATH - 1);
        return -1;
    }
    if (nonempty_str(gen_id)) {
        /*
         * In this particular case where the content is populated
         * internally, the "etc/" namespace protection is relaxed,
         * so do not emit a warning.
         */
    } else if (strncmp(name, "opt/", 4) != 0) {
        warn_report("externally provided fw_cfg item names "
                    "should be prefixed with \"opt/\"");
    }
    if (nonempty_str(str)) {
        size = strlen(str); /* NUL terminator NOT included in fw_cfg blob */
        buf = g_memdup(str, size);
    } else if (nonempty_str(gen_id)) {
        if (!fw_cfg_add_from_generator(fw_cfg, name, gen_id, errp)) {
            return -1;
        }
        return 0;
    } else {
        GError *err = NULL;
        if (!g_file_get_contents(file, &buf, &size, &err)) {
            error_setg(errp, "can't load %s: %s", file, err->message);
            g_error_free(err);
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
    DeviceState *dev;

    dev = qdev_device_add(opts, errp);
    if (!dev && *errp) {
        error_report_err(*errp);
        return -1;
    } else if (dev) {
        object_unref(OBJECT(dev));
    }
    return 0;
}

static int chardev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;

    if (!qemu_chr_new_from_opts(opts, NULL, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
            return -1;
        }
        exit(0);
    }
    return 0;
}

#ifdef CONFIG_VIRTFS
static int fsdev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    return qemu_fsdev_add(opts, errp);
}
#endif

static int mon_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    return monitor_init_opts(opts, errp);
}

static void monitor_parse(const char *optarg, const char *mode, bool pretty)
{
    static int monitor_device_index = 0;
    QemuOpts *opts;
    const char *p;
    char label[32];

    if (strstart(optarg, "chardev:", &p)) {
        snprintf(label, sizeof(label), "%s", p);
    } else {
        snprintf(label, sizeof(label), "compat_monitor%d",
                 monitor_device_index);
        opts = qemu_chr_parse_compat(label, optarg, true);
        if (!opts) {
            error_report("parse error: %s", optarg);
            exit(1);
        }
    }

    opts = qemu_opts_create(qemu_find_opts("mon"), label, 1, &error_fatal);
    qemu_opt_set(opts, "mode", mode, &error_abort);
    qemu_opt_set(opts, "chardev", label, &error_abort);
    if (!strcmp(mode, "control")) {
        qemu_opt_set_bool(opts, "pretty", pretty, &error_abort);
    } else {
        assert(pretty == false);
    }
    monitor_device_index++;
}

struct device_config {
    enum {
        DEV_USB,       /* -usbdevice     */
        DEV_SERIAL,    /* -serial        */
        DEV_PARALLEL,  /* -parallel      */
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
    int index = num_serial_hds;
    char label[32];

    if (strcmp(devname, "none") == 0)
        return 0;
    snprintf(label, sizeof(label), "serial%d", index);
    serial_hds = g_renew(Chardev *, serial_hds, index + 1);

    serial_hds[index] = qemu_chr_new_mux_mon(label, devname, NULL);
    if (!serial_hds[index]) {
        error_report("could not connect serial device"
                     " to character backend '%s'", devname);
        return -1;
    }
    num_serial_hds++;
    return 0;
}

Chardev *serial_hd(int i)
{
    assert(i >= 0);
    if (i < num_serial_hds) {
        return serial_hds[i];
    }
    return NULL;
}

int serial_max_hds(void)
{
    return num_serial_hds;
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
    parallel_hds[index] = qemu_chr_new_mux_mon(label, devname, NULL);
    if (!parallel_hds[index]) {
        error_report("could not connect parallel device"
                     " to character backend '%s'", devname);
        return -1;
    }
    index++;
    return 0;
}

static int debugcon_parse(const char *devname)
{
    QemuOpts *opts;

    if (!qemu_chr_new_mux_mon("debugcon", devname, NULL)) {
        error_report("invalid character backend '%s'", devname);
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

static MachineClass *machine_parse(const char *name, GSList *machines)
{
    MachineClass *mc;
    GSList *el;

    if (is_help_option(name)) {
        printf("Supported machines are:\n");
        machines = g_slist_sort(machines, machine_class_cmp);
        for (el = machines; el; el = el->next) {
            MachineClass *mc = el->data;
            if (mc->alias) {
                printf("%-20s %s (alias of %s)\n", mc->alias, mc->desc, mc->name);
            }
            printf("%-20s %s%s%s\n", mc->name, mc->desc,
                   mc->is_default ? " (default)" : "",
                   mc->deprecation_reason ? " (deprecated)" : "");
        }
        exit(0);
    }

    mc = find_machine(name, machines);
    if (!mc) {
        error_report("unsupported machine type");
        error_printf("Use -machine help to list supported machines\n");
        exit(1);
    }
    return mc;
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

static const char *pid_file;
static Notifier qemu_unlink_pidfile_notifier;

static void qemu_unlink_pidfile(Notifier *n, void *data)
{
    if (pid_file) {
        unlink(pid_file);
    }
}

bool machine_init_done;

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
    notifier_list_add(&machine_init_done_notifiers, notify);
    if (machine_init_done) {
        notify->notify(notify, NULL);
    }
}

void qemu_remove_machine_init_done_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

static void qemu_run_machine_init_done_notifiers(void)
{
    machine_init_done = true;
    notifier_list_notify(&machine_init_done_notifiers, NULL);
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
    GSList *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineClass *machine_class = find_default_machine(machines);
    const char *optarg;
    QemuOpts *opts;
    Location loc;

    loc_push_none(&loc);

    opts = qemu_get_machine_opts();
    qemu_opts_loc_restore(opts);

    optarg = qemu_opt_get(opts, "type");
    if (optarg) {
        machine_class = machine_parse(optarg, machines);
    }

    if (!machine_class) {
        error_report("No machine specified, and there is no default");
        error_printf("Use -machine help to list supported machines\n");
        exit(1);
    }

    loc_pop(&loc);
    g_slist_free(machines);
    return machine_class;
}

static int object_parse_property_opt(Object *obj,
                                     const char *name, const char *value,
                                     const char *skip, Error **errp)
{
    if (g_str_equal(name, skip)) {
        return 0;
    }

    if (!object_property_parse(obj, name, value, errp)) {
        return -1;
    }

    return 0;
}

static int machine_set_property(void *opaque,
                                const char *name, const char *value,
                                Error **errp)
{
    g_autofree char *qom_name = g_strdup(name);
    char *p;

    for (p = qom_name; *p; p++) {
        if (*p == '_') {
            *p = '-';
        }
    }

    /* Legacy options do not correspond to MachineState properties.  */
    if (g_str_equal(qom_name, "accel")) {
        return 0;
    }
    if (g_str_equal(qom_name, "igd-passthru")) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("xen"), qom_name, value);
        return 0;
    }
    if (g_str_equal(qom_name, "kvm-shadow-mem") ||
        g_str_equal(qom_name, "kernel-irqchip")) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("kvm"), qom_name, value);
        return 0;
    }

    return object_parse_property_opt(opaque, name, value, "type", errp);
}

/*
 * Initial object creation happens before all other
 * QEMU data types are created. The majority of objects
 * can be created at this point. The rng-egd object
 * cannot be created here, as it depends on the chardev
 * already existing.
 */
static bool object_create_initial(const char *type, QemuOpts *opts)
{
    if (user_creatable_print_help(type, opts)) {
        exit(0);
    }

    /*
     * Objects should not be made "delayed" without a reason.  If you
     * add one, state the reason in a comment!
     */

    /* Reason: rng-egd property "chardev" */
    if (g_str_equal(type, "rng-egd")) {
        return false;
    }

#if defined(CONFIG_VHOST_USER) && defined(CONFIG_LINUX)
    /* Reason: cryptodev-vhost-user property "chardev" */
    if (g_str_equal(type, "cryptodev-vhost-user")) {
        return false;
    }
#endif

    /*
     * Reason: filter-* property "netdev" etc.
     */
    if (g_str_equal(type, "filter-buffer") ||
        g_str_equal(type, "filter-dump") ||
        g_str_equal(type, "filter-mirror") ||
        g_str_equal(type, "filter-redirector") ||
        g_str_equal(type, "colo-compare") ||
        g_str_equal(type, "filter-rewriter") ||
        g_str_equal(type, "filter-replay")) {
        return false;
    }

    /* Memory allocation by backends needs to be done
     * after configure_accelerator() (due to the tcg_enabled()
     * checks at memory_region_init_*()).
     *
     * Also, allocation of large amounts of memory may delay
     * chardev initialization for too long, and trigger timeouts
     * on software that waits for a monitor socket to be created
     * (e.g. libvirt).
     */
    if (g_str_has_prefix(type, "memory-backend-")) {
        return false;
    }

    return true;
}


/*
 * The remainder of object creation happens after the
 * creation of chardev, fsdev, net clients and device data types.
 */
static bool object_create_delayed(const char *type, QemuOpts *opts)
{
    return !object_create_initial(type, opts);
}


static bool set_memory_options(uint64_t *ram_slots, ram_addr_t *maxram_size,
                               MachineClass *mc)
{
    uint64_t sz;
    const char *mem_str;
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

            sz *= MiB;
            if (sz / MiB != overflow_check) {
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
    if (mc->fixup_ram_size) {
        sz = mc->fixup_ram_size(sz);
    }
    ram_size = sz;
    if (ram_size != sz) {
        error_report("ram size too large");
        exit(EXIT_FAILURE);
    }

    /* store value for the future use */
    qemu_opt_set_number(opts, "size", ram_size, &error_abort);
    *maxram_size = ram_size;

    if (qemu_opt_get(opts, "maxmem")) {
        uint64_t slots;

        sz = qemu_opt_get_size(opts, "maxmem", 0);
        slots = qemu_opt_get_number(opts, "slots", 0);
        if (sz < ram_size) {
            error_report("invalid value of -m option maxmem: "
                         "maximum memory size (0x%" PRIx64 ") must be at least "
                         "the initial memory size (0x" RAM_ADDR_FMT ")",
                         sz, ram_size);
            exit(EXIT_FAILURE);
        } else if (slots && sz == ram_size) {
            error_report("invalid value of -m option maxmem: "
                         "memory slots were specified but maximum memory size "
                         "(0x%" PRIx64 ") is equal to the initial memory size "
                         "(0x" RAM_ADDR_FMT ")", sz, ram_size);
            exit(EXIT_FAILURE);
        }

        *maxram_size = sz;
        *ram_slots = slots;
    } else if (qemu_opt_get(opts, "slots")) {
        error_report("invalid -m option value: missing 'maxmem' option");
        exit(EXIT_FAILURE);
    }

    loc_pop(&loc);
    return !!mem_str;
}

static int global_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    GlobalProperty *g;

    g = g_malloc0(sizeof(*g));
    g->driver   = qemu_opt_get(opts, "driver");
    g->property = qemu_opt_get(opts, "property");
    g->value    = qemu_opt_get(opts, "value");
    qdev_prop_register_global(g);
    return 0;
}

static int qemu_read_default_config_file(void)
{
    int ret;

    ret = qemu_read_config_file(CONFIG_QEMU_CONFDIR "/qemu.conf");
    if (ret < 0 && ret != -ENOENT) {
        return ret;
    }

    return 0;
}

static void user_register_global_props(void)
{
    qemu_opts_foreach(qemu_find_opts("global"),
                      global_init_func, NULL, NULL);
}

static int do_configure_icount(void *opaque, QemuOpts *opts, Error **errp)
{
    configure_icount(opts, errp);
    return 0;
}

static int accelerator_set_property(void *opaque,
                                const char *name, const char *value,
                                Error **errp)
{
    return object_parse_property_opt(opaque, name, value, "accel", errp);
}

static int do_configure_accelerator(void *opaque, QemuOpts *opts, Error **errp)
{
    bool *p_init_failed = opaque;
    const char *acc = qemu_opt_get(opts, "accel");
    AccelClass *ac = accel_find(acc);
    AccelState *accel;
    int ret;

    if (!ac) {
        *p_init_failed = true;
        error_report("invalid accelerator %s", acc);
        return 0;
    }
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(ac)));
    object_apply_compat_props(OBJECT(accel));
    qemu_opt_foreach(opts, accelerator_set_property,
                     accel,
                     &error_fatal);

    ret = accel_init_machine(accel, current_machine);
    if (ret < 0) {
        *p_init_failed = true;
        error_report("failed to initialize %s: %s",
                     acc, strerror(-ret));
        return 0;
    }

    return 1;
}

static void configure_accelerators(const char *progname)
{
    const char *accel;
    bool init_failed = false;

    qemu_opts_foreach(qemu_find_opts("icount"),
                      do_configure_icount, NULL, &error_fatal);

    accel = qemu_opt_get(qemu_get_machine_opts(), "accel");
    if (QTAILQ_EMPTY(&qemu_accel_opts.head)) {
        char **accel_list, **tmp;

        if (accel == NULL) {
            /* Select the default accelerator */
            bool have_tcg = accel_find("tcg");
            bool have_kvm = accel_find("kvm");

            if (have_tcg && have_kvm) {
                if (g_str_has_suffix(progname, "kvm")) {
                    /* If the program name ends with "kvm", we prefer KVM */
                    accel = "kvm:tcg";
                } else {
                    accel = "tcg:kvm";
                }
            } else if (have_kvm) {
                accel = "kvm";
            } else if (have_tcg) {
                accel = "tcg";
            } else {
                error_report("No accelerator selected and"
                             " no default accelerator available");
                exit(1);
            }
        }
        accel_list = g_strsplit(accel, ":", 0);

        for (tmp = accel_list; *tmp; tmp++) {
            /*
             * Filter invalid accelerators here, to prevent obscenities
             * such as "-machine accel=tcg,,thread=single".
             */
            if (accel_find(*tmp)) {
                qemu_opts_parse_noisily(qemu_find_opts("accel"), *tmp, true);
            } else {
                init_failed = true;
                error_report("invalid accelerator %s", *tmp);
            }
        }
        g_strfreev(accel_list);
    } else {
        if (accel != NULL) {
            error_report("The -accel and \"-machine accel=\" options are incompatible");
            exit(1);
        }
    }

    if (!qemu_opts_foreach(qemu_find_opts("accel"),
                           do_configure_accelerator, &init_failed, &error_fatal)) {
        if (!init_failed) {
            error_report("no accelerator found");
        }
        exit(1);
    }

    if (init_failed) {
        AccelClass *ac = ACCEL_GET_CLASS(current_accel());
        error_report("falling back to %s", ac->name);
    }

    if (use_icount && !(tcg_enabled() || qtest_enabled())) {
        error_report("-icount is not allowed with hardware virtualization");
        exit(1);
    }
}

static void create_default_memdev(MachineState *ms, const char *path)
{
    Object *obj;
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    obj = object_new(path ? TYPE_MEMORY_BACKEND_FILE : TYPE_MEMORY_BACKEND_RAM);
    if (path) {
        object_property_set_str(obj, "mem-path", path, &error_fatal);
    }
    object_property_set_int(obj, "size", ms->ram_size, &error_fatal);
    object_property_add_child(object_get_objects_root(), mc->default_ram_id,
                              obj);
    /* Ensure backend's memory region name is equal to mc->default_ram_id */
    object_property_set_bool(obj, "x-use-canonical-path-for-ramblock-id",
                             false, &error_fatal);
    user_creatable_complete(USER_CREATABLE(obj), &error_fatal);
    object_unref(obj);
    object_property_set_str(OBJECT(ms), "memory-backend", mc->default_ram_id,
                            &error_fatal);
}

void qemu_init(int argc, char **argv, char **envp)
{
    int i;
    int snapshot, linux_boot;
    const char *initrd_filename;
    const char *kernel_filename, *kernel_cmdline;
    const char *boot_order = NULL;
    const char *boot_once = NULL;
    DisplayState *ds;
    QemuOpts *opts, *machine_opts;
    QemuOpts *icount_opts = NULL, *accel_opts = NULL;
    QemuOptsList *olist;
    int optind;
    const char *optarg;
    const char *loadvm = NULL;
    MachineClass *machine_class;
    const char *cpu_option;
    const char *vga_model = NULL;
    const char *qtest_chrdev = NULL;
    const char *qtest_log = NULL;
    const char *incoming = NULL;
    bool userconfig = true;
    bool nographic = false;
    int display_remote = 0;
    const char *log_mask = NULL;
    const char *log_file = NULL;
    char *trace_file = NULL;
    ram_addr_t maxram_size;
    uint64_t ram_slots = 0;
    FILE *vmstate_dump_file = NULL;
    Error *main_loop_err = NULL;
    Error *err = NULL;
    bool list_data_dirs = false;
    char *dir, **dirs;
    const char *mem_path = NULL;
    bool have_custom_ram_size;
    BlockdevOptionsQueue bdo_queue = QSIMPLEQ_HEAD_INITIALIZER(bdo_queue);
    QemuPluginList plugin_list = QTAILQ_HEAD_INITIALIZER(plugin_list);
    int mem_prealloc = 0; /* force preallocation of physical target memory */

    os_set_line_buffering();

    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);

    qemu_init_cpu_list();
    qemu_init_cpu_loop();

    qemu_mutex_lock_iothread();

    atexit(qemu_run_exit_notifiers);
    qemu_init_exec_dir(argv[0]);

    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_MIGRATION);

    qemu_add_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&qemu_legacy_drive_opts);
    qemu_add_drive_opts(&qemu_common_drive_opts);
    qemu_add_drive_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&bdrv_runtime_opts);
    qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);
    qemu_add_opts(&qemu_netdev_opts);
    qemu_add_opts(&qemu_nic_opts);
    qemu_add_opts(&qemu_net_opts);
    qemu_add_opts(&qemu_rtc_opts);
    qemu_add_opts(&qemu_global_opts);
    qemu_add_opts(&qemu_mon_opts);
    qemu_add_opts(&qemu_trace_opts);
    qemu_plugin_add_opts();
    qemu_add_opts(&qemu_option_rom_opts);
    qemu_add_opts(&qemu_machine_opts);
    qemu_add_opts(&qemu_accel_opts);
    qemu_add_opts(&qemu_mem_opts);
    qemu_add_opts(&qemu_smp_opts);
    qemu_add_opts(&qemu_boot_opts);
    qemu_add_opts(&qemu_add_fd_opts);
    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_tpmdev_opts);
    qemu_add_opts(&qemu_realtime_opts);
    qemu_add_opts(&qemu_overcommit_opts);
    qemu_add_opts(&qemu_msg_opts);
    qemu_add_opts(&qemu_name_opts);
    qemu_add_opts(&qemu_numa_opts);
    qemu_add_opts(&qemu_icount_opts);
    qemu_add_opts(&qemu_semihosting_config_opts);
    qemu_add_opts(&qemu_fw_cfg_opts);
    module_call_init(MODULE_INIT_OPTS);

    runstate_init();
    precopy_infrastructure_init();
    postcopy_infrastructure_init();
    monitor_init_globals();

    if (qcrypto_init(&err) < 0) {
        error_reportf_err(err, "cannot initialize crypto: ");
        exit(1);
    }

    QTAILQ_INIT(&vm_change_state_head);
    os_setup_early_signal_handling();

    cpu_option = NULL;
    snapshot = 0;

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
            case QEMU_OPTION_nouserconfig:
                userconfig = false;
                break;
            }
        }
    }

    if (userconfig) {
        if (qemu_read_default_config_file() < 0) {
            exit(1);
        }
    }

    /* second pass of option parsing */
    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        if (argv[optind][0] != '-') {
            loc_set_cmdline(argv, optind, 1);
            drive_add(IF_DEFAULT, 0, argv[optind++], HD_OPTS);
        } else {
            const QEMUOption *popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            if (!(popt->arch_mask & arch_type)) {
                error_report("Option not supported for this target");
                exit(1);
            }
            switch(popt->index) {
            case QEMU_OPTION_cpu:
                /* hw initialization will check this */
                cpu_option = optarg;
                break;
            case QEMU_OPTION_hda:
            case QEMU_OPTION_hdb:
            case QEMU_OPTION_hdc:
            case QEMU_OPTION_hdd:
                drive_add(IF_DEFAULT, popt->index - QEMU_OPTION_hda, optarg,
                          HD_OPTS);
                break;
            case QEMU_OPTION_blockdev:
                {
                    Visitor *v;
                    BlockdevOptionsQueueEntry *bdo;

                    v = qobject_input_visitor_new_str(optarg, "driver",
                                                      &error_fatal);

                    bdo = g_new(BlockdevOptionsQueueEntry, 1);
                    visit_type_BlockdevOptions(v, NULL, &bdo->bdo,
                                               &error_fatal);
                    visit_free(v);
                    loc_save(&bdo->loc);
                    QSIMPLEQ_INSERT_TAIL(&bdo_queue, bdo, entry);
                    break;
                }
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
                {
                    Error *blocker = NULL;
                    snapshot = 1;
                    error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED,
                               "-snapshot");
                    replay_add_blocker(blocker);
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
                parse_display(optarg);
                break;
            case QEMU_OPTION_nographic:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "graphics=off", false);
                nographic = true;
                dpy.type = DISPLAY_TYPE_NONE;
                break;
            case QEMU_OPTION_curses:
#ifdef CONFIG_CURSES
                dpy.type = DISPLAY_TYPE_CURSES;
#else
                error_report("curses or iconv support is disabled");
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
                qemu_opts_set(qemu_find_opts("machine"), NULL, "kernel", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_initrd:
                qemu_opts_set(qemu_find_opts("machine"), NULL, "initrd", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_append:
                qemu_opts_set(qemu_find_opts("machine"), NULL, "append", optarg,
                              &error_abort);
                break;
            case QEMU_OPTION_dtb:
                qemu_opts_set(qemu_find_opts("machine"), NULL, "dtb", optarg,
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
            case QEMU_OPTION_nic:
                default_net = 0;
                if (net_client_parse(qemu_find_opts("nic"), optarg) == -1) {
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
            case QEMU_OPTION_audio_help:
                audio_legacy_help();
                exit (0);
                break;
            case QEMU_OPTION_audiodev:
                audio_parse_option(optarg);
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
                qemu_set_dfilter_ranges(optarg, &error_fatal);
                break;
            case QEMU_OPTION_seed:
                qemu_guest_random_seed_main(optarg, &error_fatal);
                break;
            case QEMU_OPTION_s:
                add_device_config(DEV_GDB, "tcp::" DEFAULT_GDBSTUB_PORT);
                break;
            case QEMU_OPTION_gdb:
                add_device_config(DEV_GDB, optarg);
                break;
            case QEMU_OPTION_L:
                if (is_help_option(optarg)) {
                    list_data_dirs = true;
                } else {
                    qemu_add_data_dir(optarg);
                }
                break;
            case QEMU_OPTION_bios:
                qemu_opts_set(qemu_find_opts("machine"), NULL, "firmware", optarg,
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
                        if (depth != 1 && depth != 2 && depth != 4 &&
                            depth != 8 && depth != 15 && depth != 16 &&
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
                const char *writeout, *sock_fd, *socket, *path, *security_model,
                           *multidevs;

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
                                         qemu_opts_id(opts) ?:
                                         qemu_opt_get(opts, "mount_tag"),
                                         1, NULL);
                if (!fsdev) {
                    error_report("duplicate or invalid fsdev id: %s",
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
                path = qemu_opt_get(opts, "path");
                if (path) {
                    qemu_opt_set(fsdev, "path", path, &error_abort);
                }
                security_model = qemu_opt_get(opts, "security_model");
                if (security_model) {
                    qemu_opt_set(fsdev, "security_model", security_model,
                                 &error_abort);
                }
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
                multidevs = qemu_opt_get(opts, "multidevs");
                if (multidevs) {
                    qemu_opt_set(fsdev, "multidevs", multidevs, &error_abort);
                }
                device = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                          &error_abort);
                qemu_opt_set(device, "driver", "virtio-9p-pci", &error_abort);
                qemu_opt_set(device, "fsdev",
                             qemu_opts_id(fsdev), &error_abort);
                qemu_opt_set(device, "mount_tag",
                             qemu_opt_get(opts, "mount_tag"), &error_abort);
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
                    exit(1);
                }
                watchdog = optarg;
                break;
            case QEMU_OPTION_watchdog_action:
                if (select_watchdog_action(optarg) == -1) {
                    error_report("unknown -watchdog-action parameter");
                    exit(1);
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
                dpy.has_full_screen = true;
                dpy.full_screen = true;
                break;
            case QEMU_OPTION_alt_grab:
                alt_grab = 1;
                break;
            case QEMU_OPTION_ctrl_grab:
                ctrl_grab = 1;
                break;
            case QEMU_OPTION_no_quit:
                dpy.has_window_close = true;
                dpy.window_close = false;
                break;
            case QEMU_OPTION_sdl:
#ifdef CONFIG_SDL
                dpy.type = DISPLAY_TYPE_SDL;
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
            case QEMU_OPTION_acpitable:
                opts = qemu_opts_parse_noisily(qemu_find_opts("acpi"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                acpi_table_add(opts, &error_fatal);
                break;
            case QEMU_OPTION_smbios:
                opts = qemu_opts_parse_noisily(qemu_find_opts("smbios"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                smbios_entry_add(opts, &error_fatal);
                break;
            case QEMU_OPTION_fwcfg:
                opts = qemu_opts_parse_noisily(qemu_find_opts("fw_cfg"),
                                               optarg, true);
                if (opts == NULL) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_preconfig:
                preconfig_exit_requested = false;
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
            case QEMU_OPTION_accel:
                accel_opts = qemu_opts_parse_noisily(qemu_find_opts("accel"),
                                                     optarg, true);
                optarg = qemu_opt_get(accel_opts, "accel");
                if (!optarg || is_help_option(optarg)) {
                    printf("Accelerators supported in QEMU binary:\n");
                    GSList *el, *accel_list = object_class_get_list(TYPE_ACCEL,
                                                                    false);
                    for (el = accel_list; el; el = el->next) {
                        gchar *typename = g_strdup(object_class_get_name(
                                                   OBJECT_CLASS(el->data)));
                        /* omit qtest which is used for tests only */
                        if (g_strcmp0(typename, ACCEL_CLASS_NAME("qtest")) &&
                            g_str_has_suffix(typename, ACCEL_CLASS_SUFFIX)) {
                            gchar **optname = g_strsplit(typename,
                                                         ACCEL_CLASS_SUFFIX, 0);
                            printf("%s\n", optname[0]);
                            g_strfreev(optname);
                        }
                        g_free(typename);
                    }
                    g_slist_free(accel_list);
                    exit(0);
                }
                break;
            case QEMU_OPTION_usb:
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "usb=on", false);
                break;
            case QEMU_OPTION_usbdevice:
                error_report("'-usbdevice' is deprecated, please use "
                             "'-device usb-...' instead");
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
                olist = qemu_find_opts("machine");
                qemu_opts_parse_noisily(olist, "acpi=off", false);
                break;
            case QEMU_OPTION_no_hpet:
                no_hpet = 1;
                break;
            case QEMU_OPTION_no_reboot:
                no_reboot = 1;
                break;
            case QEMU_OPTION_no_shutdown:
                no_shutdown = 1;
                break;
            case QEMU_OPTION_show_cursor:
                warn_report("The -show-cursor option is deprecated. Please "
                            "add show-cursor=on to your -display options.");
                warn_report("When using the default display you can use "
                            "-display default,show-cursor=on");
                dpy.has_show_cursor = true;
                dpy.show_cursor = true;
                break;
            case QEMU_OPTION_uuid:
                if (qemu_uuid_parse(optarg, &qemu_uuid) < 0) {
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
                qemu_semihosting_enable();
                break;
            case QEMU_OPTION_semihosting_config:
                if (qemu_semihosting_config_options(optarg) != 0) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_name:
                opts = qemu_opts_parse_noisily(qemu_find_opts("name"),
                                               optarg, true);
                if (!opts) {
                    exit(1);
                }
                /* Capture guest name if -msg guest-name is used later */
                error_guest_name = qemu_opt_get(opts, "guest");
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
            case QEMU_OPTION_rtc:
                opts = qemu_opts_parse_noisily(qemu_find_opts("rtc"), optarg,
                                               false);
                if (!opts) {
                    exit(1);
                }
                break;
            case QEMU_OPTION_tb_size:
#ifndef CONFIG_TCG
                error_report("TCG is disabled");
                exit(1);
#endif
                warn_report("The -tb-size option is deprecated, use -accel tcg,tb-size instead");
                object_register_sugar_prop(ACCEL_CLASS_NAME("tcg"), "tb-size", optarg);
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
            case QEMU_OPTION_only_migratable:
                only_migratable = 1;
                break;
            case QEMU_OPTION_nodefaults:
                has_defaults = 0;
                break;
            case QEMU_OPTION_xen_domid:
                if (!(xen_available())) {
                    error_report("Option not supported for this target");
                    exit(1);
                }
                xen_domid = atoi(optarg);
                break;
            case QEMU_OPTION_xen_attach:
                if (!(xen_available())) {
                    error_report("Option not supported for this target");
                    exit(1);
                }
                xen_mode = XEN_ATTACH;
                break;
            case QEMU_OPTION_xen_domid_restrict:
                if (!(xen_available())) {
                    error_report("Option not supported for this target");
                    exit(1);
                }
                xen_domid_restrict = true;
                break;
            case QEMU_OPTION_trace:
                g_free(trace_file);
                trace_file = trace_opt_parse(optarg);
                break;
            case QEMU_OPTION_plugin:
                qemu_plugin_opt_parse(optarg, &plugin_list);
                break;
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
                olist = qemu_find_opts("sandbox");
                if (!olist) {
#ifndef CONFIG_SECCOMP
                    error_report("-sandbox support is not enabled "
                                 "in this QEMU binary");
#endif
                    exit(1);
                }

                opts = qemu_opts_parse_noisily(olist, optarg, true);
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
                warn_report("'-realtime mlock=...' is deprecated, please use "
                             "'-overcommit mem-lock=...' instead");
                opts = qemu_opts_parse_noisily(qemu_find_opts("realtime"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                /* Don't override the -overcommit option if set */
                enable_mlock = enable_mlock ||
                    qemu_opt_get_bool(opts, "mlock", true);
                break;
            case QEMU_OPTION_overcommit:
                opts = qemu_opts_parse_noisily(qemu_find_opts("overcommit"),
                                               optarg, false);
                if (!opts) {
                    exit(1);
                }
                /* Don't override the -realtime option if set */
                enable_mlock = enable_mlock ||
                    qemu_opt_get_bool(opts, "mem-lock", false);
                enable_cpu_pm = qemu_opt_get_bool(opts, "cpu-pm", false);
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
            case QEMU_OPTION_enable_sync_profile:
                qsp_enable();
                break;
            case QEMU_OPTION_nouserconfig:
                /* Nothing to be parsed here. Especially, do not error out below. */
                break;
            default:
                if (os_parse_cmd_args(popt->index, optarg)) {
                    error_report("Option not supported in this build");
                    exit(1);
                }
            }
        }
    }
    /*
     * Clear error location left behind by the loop.
     * Best done right after the loop.  Do not insert code here!
     */
    loc_set_none();

    /*
     * Check for -cpu help and -device help before we call select_machine(),
     * which will return an error if the architecture has no default machine
     * type and the user did not specify one, so that the user doesn't need
     * to say '-cpu help -machine something'.
     */
    if (cpu_option && is_help_option(cpu_option)) {
        list_cpus(cpu_option);
        exit(0);
    }

    if (qemu_opts_foreach(qemu_find_opts("device"),
                          device_help_func, NULL, NULL)) {
        exit(0);
    }

    user_register_global_props();

    replay_configure(icount_opts);

    if (incoming && !preconfig_exit_requested) {
        error_report("'preconfig' and 'incoming' options are "
                     "mutually exclusive");
        exit(EXIT_FAILURE);
    }

    configure_rtc(qemu_find_opts_singleton("rtc"));

    machine_class = select_machine();
    object_set_machine_compat_props(machine_class->compat_props);

    have_custom_ram_size = set_memory_options(&ram_slots, &maxram_size,
                                              machine_class);

    os_daemonize();
    rcu_disable_atfork();

    if (pid_file && !qemu_write_pidfile(pid_file, &err)) {
        error_reportf_err(err, "cannot create PID file: ");
        exit(1);
    }

    qemu_unlink_pidfile_notifier.notify = qemu_unlink_pidfile;
    qemu_add_exit_notifier(&qemu_unlink_pidfile_notifier);

    if (qemu_init_main_loop(&main_loop_err)) {
        error_report_err(main_loop_err);
        exit(1);
    }

#ifdef CONFIG_SECCOMP
    olist = qemu_find_opts_err("sandbox", NULL);
    if (olist) {
        qemu_opts_foreach(olist, parse_sandbox, NULL, &error_fatal);
    }
#endif

    qemu_opts_foreach(qemu_find_opts("name"),
                      parse_name, NULL, &error_fatal);

#ifndef _WIN32
    qemu_opts_foreach(qemu_find_opts("add-fd"),
                      parse_add_fd, NULL, &error_fatal);

    qemu_opts_foreach(qemu_find_opts("add-fd"),
                      cleanup_add_fd, NULL, &error_fatal);
#endif

    current_machine = MACHINE(object_new_with_class(OBJECT_CLASS(machine_class)));
    if (machine_help_func(qemu_get_machine_opts(), current_machine)) {
        exit(0);
    }
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_child(container_get(OBJECT(current_machine),
                                            "/unattached"),
                              "sysbus", OBJECT(sysbus_get_default()));

    if (machine_class->minimum_page_bits) {
        if (!set_preferred_target_page_bits(machine_class->minimum_page_bits)) {
            /* This would be a board error: specifying a minimum smaller than
             * a target's compile-time fixed setting.
             */
            g_assert_not_reached();
        }
    }

    cpu_exec_init_all();

    if (machine_class->hw_version) {
        qemu_set_hw_version(machine_class->hw_version);
    }

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);

    /* Open the logfile at this point and set the log mask if necessary.
     */
    qemu_set_log_filename(log_file, &error_fatal);
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

    /* add configured firmware directories */
    dirs = g_strsplit(CONFIG_QEMU_FIRMWAREPATH, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; dirs[i] != NULL; i++) {
        qemu_add_data_dir(dirs[i]);
    }
    g_strfreev(dirs);

    /* try to find datadir relative to the executable path */
    dir = os_find_datadir();
    qemu_add_data_dir(dir);
    g_free(dir);

    /* add the datadir specified when building */
    qemu_add_data_dir(CONFIG_QEMU_DATADIR);

    /* -L help lists the data directories and exits. */
    if (list_data_dirs) {
        for (i = 0; i < data_dir_idx; i++) {
            printf("%s\n", data_dir[i]);
        }
        exit(0);
    }

    /* machine_class: default to UP */
    machine_class->max_cpus = machine_class->max_cpus ?: 1;
    machine_class->min_cpus = machine_class->min_cpus ?: 1;
    machine_class->default_cpus = machine_class->default_cpus ?: 1;

    /* default to machine_class->default_cpus */
    current_machine->smp.cpus = machine_class->default_cpus;
    current_machine->smp.max_cpus = machine_class->default_cpus;
    current_machine->smp.cores = 1;
    current_machine->smp.threads = 1;
    current_machine->smp.sockets = 1;

    machine_class->smp_parse(current_machine,
        qemu_opts_find(qemu_find_opts("smp-opts"), NULL));

    /* sanity-check smp_cpus and max_cpus against machine_class */
    if (current_machine->smp.cpus < machine_class->min_cpus) {
        error_report("Invalid SMP CPUs %d. The min CPUs "
                     "supported by machine '%s' is %d",
                     current_machine->smp.cpus,
                     machine_class->name, machine_class->min_cpus);
        exit(1);
    }
    if (current_machine->smp.max_cpus > machine_class->max_cpus) {
        error_report("Invalid SMP CPUs %d. The max CPUs "
                     "supported by machine '%s' is %d",
                     current_machine->smp.max_cpus,
                     machine_class->name, machine_class->max_cpus);
        exit(1);
    }

    if (mem_prealloc) {
        char *val;

        val = g_strdup_printf("%d", current_machine->smp.cpus);
        object_register_sugar_prop("memory-backend", "prealloc-threads", val);
        g_free(val);
        object_register_sugar_prop("memory-backend", "prealloc", "on");
    }

    /*
     * Get the default machine options from the machine if it is not already
     * specified either by the configuration file or by the command line.
     */
    if (machine_class->default_machine_opts) {
        qemu_opts_set_defaults(qemu_find_opts("machine"),
                               machine_class->default_machine_opts, 0);
    }

    /* process plugin before CPUs are created, but once -smp has been parsed */
    if (qemu_plugin_load_list(&plugin_list)) {
        exit(1);
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
        if (!preconfig_exit_requested) {
            error_report("'preconfig' and 'daemonize' options are "
                         "mutually exclusive");
            exit(EXIT_FAILURE);
        }

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
            && (default_parallel || default_serial || default_monitor)) {
            error_report("-nographic cannot be used with -daemonize");
            exit(1);
        }
#ifdef CONFIG_CURSES
        if (dpy.type == DISPLAY_TYPE_CURSES) {
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
        } else {
            if (default_serial)
                add_device_config(DEV_SERIAL, "stdio");
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
    }

#if defined(CONFIG_VNC)
    if (!QTAILQ_EMPTY(&(qemu_find_opts("vnc")->head))) {
        display_remote++;
    }
#endif
    if (dpy.type == DISPLAY_TYPE_DEFAULT && !display_remote) {
        if (!qemu_display_find_default(&dpy)) {
            dpy.type = DISPLAY_TYPE_NONE;
#if defined(CONFIG_VNC)
            vnc_parse("localhost:0,to=99,id=default", &error_abort);
#endif
        }
    }
    if (dpy.type == DISPLAY_TYPE_DEFAULT) {
        dpy.type = DISPLAY_TYPE_NONE;
    }

    if ((alt_grab || ctrl_grab) && dpy.type != DISPLAY_TYPE_SDL) {
        error_report("-alt-grab and -ctrl-grab are only valid "
                     "for SDL, ignoring option");
    }
    if (dpy.has_window_close &&
        (dpy.type != DISPLAY_TYPE_GTK && dpy.type != DISPLAY_TYPE_SDL)) {
        error_report("-no-quit is only valid for GTK and SDL, "
                     "ignoring option");
    }

    qemu_display_early_init(&dpy);
    qemu_console_early_init();

    if (dpy.has_gl && dpy.gl != DISPLAYGL_MODE_OFF && display_opengl == 0) {
#if defined(CONFIG_OPENGL)
        error_report("OpenGL is not supported by the display");
#else
        error_report("OpenGL support is disabled");
#endif
        exit(1);
    }

    page_size_init();
    socket_init();

    qemu_opts_foreach(qemu_find_opts("object"),
                      user_creatable_add_opts_foreach,
                      object_create_initial, &error_fatal);

    qemu_opts_foreach(qemu_find_opts("chardev"),
                      chardev_init_func, NULL, &error_fatal);

#ifdef CONFIG_VIRTFS
    qemu_opts_foreach(qemu_find_opts("fsdev"),
                      fsdev_init_func, NULL, &error_fatal);
#endif

    /* spice needs the timers to be initialized by this point */
    /* spice must initialize before audio as it changes the default auiodev */
    qemu_spice_init();

    /*
     * Note: we need to create audio and block backends before
     * machine_set_property(), so machine properties can refer to
     * them.
     */
    configure_blockdev(&bdo_queue, machine_class, snapshot);
    audio_init_audiodevs();

    machine_opts = qemu_get_machine_opts();
    qemu_opt_foreach(machine_opts, machine_set_property, current_machine,
                     &error_fatal);
    current_machine->ram_size = ram_size;
    current_machine->maxram_size = maxram_size;
    current_machine->ram_slots = ram_slots;

    /*
     * Note: uses machine properties such as kernel-irqchip, must run
     * after machine_set_property().
     */
    configure_accelerators(argv[0]);

    /*
     * Beware, QOM objects created before this point miss global and
     * compat properties.
     *
     * Global properties get set up by qdev_prop_register_global(),
     * called from user_register_global_props(), and certain option
     * desugaring.  Also in CPU feature desugaring (buried in
     * parse_cpu_option()), which happens below this point, but may
     * only target the CPU type, which can only be created after
     * parse_cpu_option() returned the type.
     *
     * Machine compat properties: object_set_machine_compat_props().
     * Accelerator compat props: object_set_accelerator_compat_props(),
     * called from configure_accelerator().
     */

    if (!qtest_enabled() && machine_class->deprecation_reason) {
        error_report("Machine type '%s' is deprecated: %s",
                     machine_class->name, machine_class->deprecation_reason);
    }

    /*
     * Note: creates a QOM object, must run only after global and
     * compat properties have been set up.
     */
    migration_object_init();

    if (qtest_chrdev) {
        qtest_server_init(qtest_chrdev, qtest_log, &error_fatal);
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

    if (semihosting_enabled() && !semihosting_get_argc() && kernel_filename) {
        /* fall back to the -kernel/-append */
        semihosting_arg_fallback(kernel_filename, kernel_cmdline);
    }

    cpu_ticks_init();

    if (default_net) {
        QemuOptsList *net = qemu_find_opts("net");
        qemu_opts_set(net, NULL, "type", "nic", &error_abort);
#ifdef CONFIG_SLIRP
        qemu_opts_set(net, NULL, "type", "user", &error_abort);
#endif
    }

    if (net_init_clients(&err) < 0) {
        error_report_err(err);
        exit(1);
    }

    qemu_opts_foreach(qemu_find_opts("object"),
                      user_creatable_add_opts_foreach,
                      object_create_delayed, &error_fatal);

    if (tpm_init() < 0) {
        exit(1);
    }

    blk_mig_init();
    ram_mig_init();
    dirty_bitmap_mig_init();

    qemu_opts_foreach(qemu_find_opts("mon"),
                      mon_init_func, NULL, &error_fatal);

    /* connect semihosting console input if requested */
    qemu_semihosting_console_init();

    if (foreach_device_config(DEV_SERIAL, serial_parse) < 0)
        exit(1);
    if (foreach_device_config(DEV_PARALLEL, parallel_parse) < 0)
        exit(1);
    if (foreach_device_config(DEV_DEBUGCON, debugcon_parse) < 0)
        exit(1);

    /* now chardevs have been created we may have semihosting to connect */
    qemu_semihosting_connect_chardevs();

    /* If no default VGA is requested, the default is "none".  */
    if (default_vga) {
        vga_model = get_default_vga_model(machine_class);
    }
    if (vga_model) {
        select_vgahw(machine_class, vga_model);
    }

    if (watchdog) {
        i = select_watchdog(watchdog);
        if (i > 0)
            exit (i == 1 ? 1 : 0);
    }

    /* This checkpoint is required by replay to separate prior clock
       reading from the other reads, because timer polling functions query
       clock values from the log. */
    replay_checkpoint(CHECKPOINT_INIT);
    qdev_machine_init();

    current_machine->boot_order = boot_order;

    /* parse features once if machine provides default cpu_type */
    current_machine->cpu_type = machine_class->default_cpu_type;
    if (cpu_option) {
        current_machine->cpu_type = parse_cpu_option(cpu_option);
    }

    if (current_machine->ram_memdev_id) {
        Object *backend;
        ram_addr_t backend_size;

        backend = object_resolve_path_type(current_machine->ram_memdev_id,
                                           TYPE_MEMORY_BACKEND, NULL);
        if (!backend) {
            error_report("Memory backend '%s' not found",
                         current_machine->ram_memdev_id);
            exit(EXIT_FAILURE);
        }
        backend_size = object_property_get_uint(backend, "size",  &error_abort);
        if (have_custom_ram_size && backend_size != ram_size) {
                error_report("Size specified by -m option must match size of "
                             "explicitly specified 'memory-backend' property");
                exit(EXIT_FAILURE);
        }
        if (mem_path) {
            error_report("'-mem-path' can't be used together with"
                         "'-machine memory-backend'");
            exit(EXIT_FAILURE);
        }
        ram_size = backend_size;
    }

    if (!xen_enabled()) {
        /* On 32-bit hosts, QEMU is limited by virtual address space */
        if (ram_size > (2047 << 20) && HOST_LONG_BITS == 32) {
            error_report("at most 2047 MB RAM can be simulated");
            exit(1);
        }
    }

    parse_numa_opts(current_machine);

    /* do monitor/qmp handling at preconfig state if requested */
    qemu_main_loop();

    if (machine_class->default_ram_id && current_machine->ram_size &&
        numa_uses_legacy_mem() && !current_machine->ram_memdev_id) {
        create_default_memdev(current_machine, mem_path);
    }

    /* from here on runstate is RUN_STATE_PRELAUNCH */
    machine_run_board_init(current_machine);

    /*
     * TODO To drop support for deprecated bogus if=..., move
     * drive_check_orphaned() here, replacing this call.  Also drop
     * its deprecation warning, along with DriveInfo member
     * @claimed_by_board.
     */
    drive_mark_claimed_by_board();

    realtime_init();

    soundhw_init();

    if (hax_enabled()) {
        hax_sync_vcpus();
    }

    qemu_opts_foreach(qemu_find_opts("fw_cfg"),
                      parse_fw_cfg, fw_cfg_find(), &error_fatal);

    /* init USB devices */
    if (machine_usb(current_machine)) {
        if (foreach_device_config(DEV_USB, usb_parse) < 0)
            exit(1);
    }

    /* init generic devices */
    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_DEVICE);
    qemu_opts_foreach(qemu_find_opts("device"),
                      device_init_func, NULL, &error_fatal);

    cpu_synchronize_all_post_init();

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
    if (!default_net && (!qtest_enabled() || has_defaults)) {
        net_check_clients();
    }

    if (boot_once) {
        qemu_boot_set(boot_once, &error_fatal);
        qemu_register_reset(restore_boot_order, g_strdup(boot_order));
    }

    /* init local displays */
    ds = init_displaystate();
    qemu_display_init(ds, &dpy);

    /* must be after terminal init, SDL library changes signal handlers */
    os_setup_signal_handling();

    /* init remote displays */
#ifdef CONFIG_VNC
    qemu_opts_foreach(qemu_find_opts("vnc"),
                      vnc_init_func, NULL, &error_fatal);
#endif

    if (using_spice) {
        qemu_spice_display_init();
    }

    if (foreach_device_config(DEV_GDB, gdbserver_start) < 0) {
        exit(1);
    }

    qdev_machine_creation_done();

    /* TODO: once all bus devices are qdevified, this should be done
     * when bus is created by qdev.c */
    /*
     * TODO: If we had a main 'reset container' that the whole system
     * lived in, we could reset that using the multi-phase reset
     * APIs. For the moment, we just reset the sysbus, which will cause
     * all devices hanging off it (and all their child buses, recursively)
     * to be reset. Note that this will *not* reset any Device objects
     * which are not attached to some part of the qbus tree!
     */
    qemu_register_reset(resettable_cold_reset_fn, sysbus_get_default());
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
    qemu_system_reset(SHUTDOWN_CAUSE_NONE);
    register_global_state();
    if (loadvm) {
        Error *local_err = NULL;
        if (load_snapshot(loadvm, &local_err) < 0) {
            error_report_err(local_err);
            autostart = 0;
            exit(1);
        }
    }
    if (replay_mode != REPLAY_MODE_NONE) {
        replay_vmstate_init();
    }

    qdev_prop_check_globals();
    if (vmstate_dump_file) {
        /* dump and exit */
        dump_vmstate_json_to_file(vmstate_dump_file);
        exit(0);
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

    accel_setup_post(current_machine);
    os_setup_post();

    return;
}

void qemu_cleanup(void)
{
    gdbserver_cleanup();

    /*
     * cleaning up the migration object cancels any existing migration
     * try to do this early so that it also stops using devices.
     */
    migration_shutdown();

    /*
     * We must cancel all block jobs while the block layer is drained,
     * or cancelling will be affected by throttling and thus may block
     * for an extended period of time.
     * vm_shutdown() will bdrv_drain_all(), so we may as well include
     * it in the drained section.
     * We do not need to end this section, because we do not want any
     * requests happening from here on anyway.
     */
    bdrv_drain_all_begin();

    /* No more vcpu or device emulation activity beyond this point */
    vm_shutdown();
    replay_finish();

    job_cancel_sync_all();
    bdrv_close_all();

    res_free();

    /* vhost-user must be cleaned up before chardevs.  */
    tpm_cleanup();
    net_cleanup();
    audio_cleanup();
    monitor_cleanup();
    qemu_chr_cleanup();
    user_creatable_cleanup();
    /* TODO: unref root container, check all devices are ok */
}
