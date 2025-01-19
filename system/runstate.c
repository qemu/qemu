/*
 * QEMU main system emulation loop
 *
 * Copyright (c) 2003-2020 QEMU contributors
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
#include "audio/audio.h"
#include "block/block.h"
#include "block/export.h"
#include "chardev/char.h"
#include "crypto/cipher.h"
#include "crypto/init.h"
#include "exec/cpu-common.h"
#include "gdbstub/syscalls.h"
#include "hw/boards.h"
#include "hw/resettable.h"
#include "migration/misc.h"
#include "migration/postcopy-ram.h"
#include "monitor/monitor.h"
#include "net/net.h"
#include "net/vhost_net.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-events-run-state.h"
#include "qemu/accel.h"
#include "qemu/error-report.h"
#include "qemu/job.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "qemu/thread.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "system/cpus.h"
#include "system/qtest.h"
#include "system/replay.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/runstate-action.h"
#include "system/system.h"
#include "system/tpm.h"
#include "trace.h"

static NotifierList exit_notifiers =
    NOTIFIER_LIST_INITIALIZER(exit_notifiers);

static RunState current_run_state = RUN_STATE_PRELAUNCH;

/* We use RUN_STATE__MAX but any invalid value will do */
static RunState vmstop_requested = RUN_STATE__MAX;
static QemuMutex vmstop_lock;

typedef struct {
    RunState from;
    RunState to;
} RunStateTransition;

static const RunStateTransition runstate_transitions_def[] = {
    { RUN_STATE_PRELAUNCH, RUN_STATE_INMIGRATE },
    { RUN_STATE_PRELAUNCH, RUN_STATE_SUSPENDED },

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
    { RUN_STATE_PAUSED, RUN_STATE_SUSPENDED},

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
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_COLO },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_INTERNAL_ERROR },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_IO_ERROR },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_SHUTDOWN },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_SUSPENDED },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_WATCHDOG },
    { RUN_STATE_FINISH_MIGRATE, RUN_STATE_GUEST_PANICKED },

    { RUN_STATE_RESTORE_VM, RUN_STATE_RUNNING },
    { RUN_STATE_RESTORE_VM, RUN_STATE_PRELAUNCH },
    { RUN_STATE_RESTORE_VM, RUN_STATE_SUSPENDED },

    { RUN_STATE_COLO, RUN_STATE_RUNNING },
    { RUN_STATE_COLO, RUN_STATE_PRELAUNCH },
    { RUN_STATE_COLO, RUN_STATE_SHUTDOWN},

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
    { RUN_STATE_SAVE_VM, RUN_STATE_SUSPENDED },

    { RUN_STATE_SHUTDOWN, RUN_STATE_PAUSED },
    { RUN_STATE_SHUTDOWN, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SHUTDOWN, RUN_STATE_PRELAUNCH },
    { RUN_STATE_SHUTDOWN, RUN_STATE_COLO },

    { RUN_STATE_DEBUG, RUN_STATE_SUSPENDED },
    { RUN_STATE_RUNNING, RUN_STATE_SUSPENDED },
    { RUN_STATE_SUSPENDED, RUN_STATE_RUNNING },
    { RUN_STATE_SUSPENDED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_SUSPENDED, RUN_STATE_PRELAUNCH },
    { RUN_STATE_SUSPENDED, RUN_STATE_COLO},
    { RUN_STATE_SUSPENDED, RUN_STATE_PAUSED},
    { RUN_STATE_SUSPENDED, RUN_STATE_SAVE_VM },
    { RUN_STATE_SUSPENDED, RUN_STATE_RESTORE_VM },
    { RUN_STATE_SUSPENDED, RUN_STATE_SHUTDOWN },

    { RUN_STATE_WATCHDOG, RUN_STATE_RUNNING },
    { RUN_STATE_WATCHDOG, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_WATCHDOG, RUN_STATE_PRELAUNCH },
    { RUN_STATE_WATCHDOG, RUN_STATE_COLO},

    { RUN_STATE_GUEST_PANICKED, RUN_STATE_RUNNING },
    { RUN_STATE_GUEST_PANICKED, RUN_STATE_FINISH_MIGRATE },
    { RUN_STATE_GUEST_PANICKED, RUN_STATE_PRELAUNCH },

    { RUN_STATE__MAX, RUN_STATE__MAX },
};

static const RunStateTransition replay_play_runstate_transitions_def[] = {
    { RUN_STATE_SHUTDOWN, RUN_STATE_RUNNING},

    { RUN_STATE__MAX, RUN_STATE__MAX },
};

static bool runstate_valid_transitions[RUN_STATE__MAX][RUN_STATE__MAX];

bool runstate_check(RunState state)
{
    return current_run_state == state;
}

static void transitions_set_valid(const RunStateTransition *rst)
{
    const RunStateTransition *p;

    for (p = rst; p->from != RUN_STATE__MAX; p++) {
        runstate_valid_transitions[p->from][p->to] = true;
    }
}

void runstate_replay_enable(void)
{
    assert(replay_mode != REPLAY_MODE_NONE);

    if (replay_mode == REPLAY_MODE_PLAY) {
        /*
         * When reverse-debugging, it is possible to move state from
         * shutdown to running.
         */
        transitions_set_valid(&replay_play_runstate_transitions_def[0]);
    }
}

static void runstate_init(void)
{
    memset(&runstate_valid_transitions, 0, sizeof(runstate_valid_transitions));

    transitions_set_valid(&runstate_transitions_def[0]);

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

RunState runstate_get(void)
{
    return current_run_state;
}

bool runstate_is_running(void)
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
struct VMChangeStateEntry {
    VMChangeStateHandler *cb;
    VMChangeStateHandler *prepare_cb;
    void *opaque;
    QTAILQ_ENTRY(VMChangeStateEntry) entries;
    int priority;
};

static QTAILQ_HEAD(, VMChangeStateEntry) vm_change_state_head =
    QTAILQ_HEAD_INITIALIZER(vm_change_state_head);

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
    return qemu_add_vm_change_state_handler_prio_full(cb, NULL, opaque,
                                                      priority);
}

/**
 * qemu_add_vm_change_state_handler_prio_full:
 * @cb: the main callback to invoke
 * @prepare_cb: a callback to invoke before the main callback
 * @opaque: user data passed to the callbacks
 * @priority: low priorities execute first when the vm runs and the reverse is
 *            true when the vm stops
 *
 * Register a main callback function and an optional prepare callback function
 * that are invoked when the vm starts or stops running. The main callback and
 * the prepare callback are called in two separate phases: First all prepare
 * callbacks are called and only then all main callbacks are called. As its
 * name suggests, the prepare callback can be used to do some preparatory work
 * before invoking the main callback.
 *
 * Returns: an entry to be freed using qemu_del_vm_change_state_handler()
 */
VMChangeStateEntry *
qemu_add_vm_change_state_handler_prio_full(VMChangeStateHandler *cb,
                                           VMChangeStateHandler *prepare_cb,
                                           void *opaque, int priority)
{
    VMChangeStateEntry *e;
    VMChangeStateEntry *other;

    e = g_malloc0(sizeof(*e));
    e->cb = cb;
    e->prepare_cb = prepare_cb;
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

void vm_state_notify(bool running, RunState state)
{
    VMChangeStateEntry *e, *next;

    trace_vm_state_notify(running, state, RunState_str(state));

    if (running) {
        QTAILQ_FOREACH_SAFE(e, &vm_change_state_head, entries, next) {
            if (e->prepare_cb) {
                e->prepare_cb(e->opaque, running, state);
            }
        }

        QTAILQ_FOREACH_SAFE(e, &vm_change_state_head, entries, next) {
            e->cb(e->opaque, running, state);
        }
    } else {
        QTAILQ_FOREACH_REVERSE_SAFE(e, &vm_change_state_head, entries, next) {
            if (e->prepare_cb) {
                e->prepare_cb(e->opaque, running, state);
            }
        }

        QTAILQ_FOREACH_REVERSE_SAFE(e, &vm_change_state_head, entries, next) {
            e->cb(e->opaque, running, state);
        }
    }
}

static ShutdownCause reset_requested;
static ShutdownCause shutdown_requested;
static int shutdown_exit_code = EXIT_SUCCESS;
static int shutdown_signal;
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
    return qatomic_xchg(&shutdown_requested, SHUTDOWN_CAUSE_NONE);
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

/*
 * Reset the VM. Issue an event unless @reason is SHUTDOWN_CAUSE_NONE.
 */
void qemu_system_reset(ShutdownCause reason)
{
    MachineClass *mc;
    ResetType type;

    mc = current_machine ? MACHINE_GET_CLASS(current_machine) : NULL;

    cpu_synchronize_all_states();

    switch (reason) {
    case SHUTDOWN_CAUSE_SNAPSHOT_LOAD:
        type = RESET_TYPE_SNAPSHOT_LOAD;
        break;
    default:
        type = RESET_TYPE_COLD;
    }
    if (mc && mc->reset) {
        mc->reset(current_machine, type);
    } else {
        qemu_devices_reset(type);
    }
    switch (reason) {
    case SHUTDOWN_CAUSE_NONE:
    case SHUTDOWN_CAUSE_SUBSYSTEM_RESET:
    case SHUTDOWN_CAUSE_SNAPSHOT_LOAD:
        break;
    default:
        qapi_event_send_reset(shutdown_caused_by_guest(reason), reason);
    }

    /*
     * Some boards use the machine reset callback to point CPUs to the firmware
     * entry point.  Assume that this is not the case for boards that support
     * non-resettable CPUs (currently used only for confidential guests), in
     * which case cpu_synchronize_all_post_init() is enough because
     * it does _more_  than cpu_synchronize_all_post_reset().
     */
    if (cpus_are_resettable()) {
        cpu_synchronize_all_post_reset();
    } else {
        assert(runstate_check(RUN_STATE_PRELAUNCH));
    }

    vm_set_suspended(false);
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
    /*
     * TODO:  Currently the available panic actions are: none, pause, and
     * shutdown, but in principle debug and reset could be supported as well.
     * Investigate any potential use cases for the unimplemented actions.
     */
    if (panic_action == PANIC_ACTION_PAUSE
        || (panic_action == PANIC_ACTION_SHUTDOWN && shutdown_action == SHUTDOWN_ACTION_PAUSE)) {
        qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_PAUSE, info);
        vm_stop(RUN_STATE_GUEST_PANICKED);
    } else if (panic_action == PANIC_ACTION_SHUTDOWN ||
               panic_action == PANIC_ACTION_EXIT_FAILURE) {
        qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_POWEROFF, info);
        vm_stop(RUN_STATE_GUEST_PANICKED);
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_PANIC);
    } else {
        qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_RUN, info);
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
    qapi_event_send_guest_crashloaded(GUEST_PANIC_ACTION_RUN, info);
    qapi_free_GuestPanicInformation(info);
}

void qemu_system_guest_pvshutdown(void)
{
    qapi_event_send_guest_pvshutdown();
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
}

void qemu_system_reset_request(ShutdownCause reason)
{
    if (reboot_action == REBOOT_ACTION_SHUTDOWN &&
        reason != SHUTDOWN_CAUSE_SUBSYSTEM_RESET) {
        shutdown_requested = reason;
    } else if (!cpus_are_resettable()) {
        error_report("cpus are not resettable, terminating");
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

static bool wakeup_suspend_enabled;

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
    shutdown_action = SHUTDOWN_ACTION_POWEROFF;

    /* Cannot call qemu_system_shutdown_request directly because
     * we are in a signal handler.
     */
    shutdown_requested = SHUTDOWN_CAUSE_HOST_SIGNAL;
    qemu_notify_event();
}

void qemu_system_shutdown_request_with_code(ShutdownCause reason,
                                            int exit_code)
{
    shutdown_exit_code = exit_code;
    qemu_system_shutdown_request(reason);
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

static bool main_loop_should_exit(int *status)
{
    RunState r;
    ShutdownCause request;

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
        if (shutdown_action == SHUTDOWN_ACTION_PAUSE) {
            vm_stop(RUN_STATE_SHUTDOWN);
        } else {
            if (shutdown_exit_code != EXIT_SUCCESS) {
                *status = shutdown_exit_code;
            } else if (request == SHUTDOWN_CAUSE_GUEST_PANIC &&
                panic_action == PANIC_ACTION_EXIT_FAILURE) {
                *status = EXIT_FAILURE;
            }
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

int qemu_main_loop(void)
{
    int status = EXIT_SUCCESS;

    while (!main_loop_should_exit(&status)) {
        main_loop_wait(false);
    }

    return status;
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
    BQL_LOCK_GUARD();
    notifier_list_notify(&exit_notifiers, NULL);
}

void qemu_init_subsystems(void)
{
    Error *err = NULL;

    os_set_line_buffering();

    module_call_init(MODULE_INIT_TRACE);

    qemu_init_cpu_list();
    qemu_init_cpu_loop();
    bql_lock();

    atexit(qemu_run_exit_notifiers);

    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_MIGRATION);

    runstate_init();
    precopy_infrastructure_init();
    postcopy_infrastructure_init();
    monitor_init_globals();

    if (qcrypto_init(&err) < 0) {
        error_reportf_err(err, "cannot initialize crypto: ");
        exit(1);
    }

    os_setup_early_signal_handling();

    bdrv_init_with_whitelist();
    socket_init();
}


void qemu_cleanup(int status)
{
    gdb_exit(status);

    /*
     * cleaning up the migration object cancels any existing migration
     * try to do this early so that it also stops using devices.
     */
    migration_shutdown();

    /*
     * Close the exports before draining the block layer. The export
     * drivers may have coroutines yielding on it, so we need to clean
     * them up before the drain, as otherwise they may be get stuck in
     * blk_wait_while_drained().
     */
    blk_exp_close_all();


    /* No more vcpu or device emulation activity beyond this point */
    vm_shutdown();
    replay_finish();

    /*
     * We must cancel all block jobs while the block layer is drained,
     * or cancelling will be affected by throttling and thus may block
     * for an extended period of time.
     * Begin the drained section after vm_shutdown() to avoid requests being
     * stuck in the BlockBackend's request queue.
     * We do not need to end this section, because we do not want any
     * requests happening from here on anyway.
     */
    bdrv_drain_all_begin();
    job_cancel_sync_all();
    bdrv_close_all();

    /* vhost-user must be cleaned up before chardevs.  */
    tpm_cleanup();
    net_cleanup();
    audio_cleanup();
    monitor_cleanup();
    qemu_chr_cleanup();
    user_creatable_cleanup();
    /* TODO: unref root container, check all devices are ok */
}
