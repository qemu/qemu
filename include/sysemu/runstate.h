#ifndef SYSEMU_RUNSTATE_H
#define SYSEMU_RUNSTATE_H

#include "qapi/qapi-types-run-state.h"
#include "qemu/notify.h"

bool runstate_check(RunState state);
void runstate_set(RunState new_state);
bool runstate_is_running(void);
bool runstate_needs_reset(void);
bool runstate_store(char *str, size_t size);

typedef void VMChangeStateHandler(void *opaque, bool running, RunState state);

VMChangeStateEntry *qemu_add_vm_change_state_handler(VMChangeStateHandler *cb,
                                                     void *opaque);
VMChangeStateEntry *qemu_add_vm_change_state_handler_prio(
        VMChangeStateHandler *cb, void *opaque, int priority);
VMChangeStateEntry *qdev_add_vm_change_state_handler(DeviceState *dev,
                                                     VMChangeStateHandler *cb,
                                                     void *opaque);
void qemu_del_vm_change_state_handler(VMChangeStateEntry *e);
/**
 * vm_state_notify: Notify the state of the VM
 *
 * @running: whether the VM is running or not.
 * @state: the #RunState of the VM.
 */
void vm_state_notify(bool running, RunState state);

static inline bool shutdown_caused_by_guest(ShutdownCause cause)
{
    return cause >= SHUTDOWN_CAUSE_GUEST_SHUTDOWN;
}

void vm_start(void);
int vm_prepare_start(void);
int vm_stop(RunState state);
int vm_stop_force_state(RunState state);
int vm_shutdown(void);

typedef enum WakeupReason {
    /* Always keep QEMU_WAKEUP_REASON_NONE = 0 */
    QEMU_WAKEUP_REASON_NONE = 0,
    QEMU_WAKEUP_REASON_RTC,
    QEMU_WAKEUP_REASON_PMTIMER,
    QEMU_WAKEUP_REASON_OTHER,
} WakeupReason;

void qemu_system_reset_request(ShutdownCause reason);
void qemu_system_suspend_request(void);
void qemu_register_suspend_notifier(Notifier *notifier);
bool qemu_wakeup_suspend_enabled(void);
void qemu_system_wakeup_request(WakeupReason reason, Error **errp);
void qemu_system_wakeup_enable(WakeupReason reason, bool enabled);
void qemu_register_wakeup_notifier(Notifier *notifier);
void qemu_register_wakeup_support(void);
void qemu_system_shutdown_request(ShutdownCause reason);
void qemu_system_powerdown_request(void);
void qemu_register_powerdown_notifier(Notifier *notifier);
void qemu_register_shutdown_notifier(Notifier *notifier);
void qemu_system_debug_request(void);
void qemu_system_vmstop_request(RunState reason);
void qemu_system_vmstop_request_prepare(void);
bool qemu_vmstop_requested(RunState *r);
ShutdownCause qemu_shutdown_requested_get(void);
ShutdownCause qemu_reset_requested_get(void);
void qemu_system_killed(int signal, pid_t pid);
void qemu_system_reset(ShutdownCause reason);
void qemu_system_guest_panicked(GuestPanicInformation *info);
void qemu_system_guest_crashloaded(GuestPanicInformation *info);

#endif

