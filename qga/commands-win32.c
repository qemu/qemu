/*
 * QEMU Guest Agent win32-specific command implementations
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *  Gal Hammer        <ghammer@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <wtypes.h>
#include <powrprof.h>
#include "qga/guest-agent-core.h"
#include "qga/vss-win32.h"
#include "qga-qmp-commands.h"
#include "qapi/qmp/qerror.h"

#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif

/* multiple of 100 nanoseconds elapsed between windows baseline
 *    (1/1/1601) and Unix Epoch (1/1/1970), accounting for leap years */
#define W32_FT_OFFSET (10000000ULL * 60 * 60 * 24 * \
                       (365 * (1970 - 1601) +       \
                        (1970 - 1601) / 4 - 3))

static void acquire_privilege(const char *name, Error **errp)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES priv;
    Error *local_err = NULL;

    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token))
    {
        if (!LookupPrivilegeValue(NULL, name, &priv.Privileges[0].Luid)) {
            error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                      "no luid for requested privilege");
            goto out;
        }

        priv.PrivilegeCount = 1;
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(token, FALSE, &priv, 0, NULL, 0)) {
            error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                      "unable to acquire requested privilege");
            goto out;
        }

    } else {
        error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                  "failed to open privilege token");
    }

out:
    if (token) {
        CloseHandle(token);
    }
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static void execute_async(DWORD WINAPI (*func)(LPVOID), LPVOID opaque,
                          Error **errp)
{
    Error *local_err = NULL;

    HANDLE thread = CreateThread(NULL, 0, func, opaque, 0, NULL);
    if (!thread) {
        error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                  "failed to dispatch asynchronous command");
        error_propagate(errp, local_err);
    }
}

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **errp)
{
    Error *local_err = NULL;
    UINT shutdown_flag = EWX_FORCE;

    slog("guest-shutdown called, mode: %s", mode);

    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag |= EWX_POWEROFF;
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag |= EWX_SHUTDOWN;
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag |= EWX_REBOOT;
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "mode",
                  "halt|powerdown|reboot");
        return;
    }

    /* Request a shutdown privilege, but try to shut down the system
       anyway. */
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!ExitWindowsEx(shutdown_flag, SHTDN_REASON_FLAG_PLANNED)) {
        slog("guest-shutdown failed: %lu", GetLastError());
        error_set(errp, QERR_UNDEFINED_ERROR);
    }
}

int64_t qmp_guest_file_open(const char *path, bool has_mode, const char *mode,
                            Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return 0;
}

void qmp_guest_file_close(int64_t handle, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                   int64_t count, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return 0;
}

GuestFileWrite *qmp_guest_file_write(int64_t handle, const char *buf_b64,
                                     bool has_count, int64_t count,
                                     Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return 0;
}

GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                   int64_t whence, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return 0;
}

void qmp_guest_file_flush(int64_t handle, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **errp)
{
    if (!vss_initialized()) {
        error_set(errp, QERR_UNSUPPORTED);
        return 0;
    }

    if (ga_is_frozen(ga_state)) {
        return GUEST_FSFREEZE_STATUS_FROZEN;
    }

    return GUEST_FSFREEZE_STATUS_THAWED;
}

/*
 * Freeze local file systems using Volume Shadow-copy Service.
 * The frozen state is limited for up to 10 seconds by VSS.
 */
int64_t qmp_guest_fsfreeze_freeze(Error **errp)
{
    int i;
    Error *local_err = NULL;

    if (!vss_initialized()) {
        error_set(errp, QERR_UNSUPPORTED);
        return 0;
    }

    slog("guest-fsfreeze called");

    /* cannot risk guest agent blocking itself on a write in this state */
    ga_set_frozen(ga_state);

    qga_vss_fsfreeze(&i, &local_err, true);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    return i;

error:
    local_err = NULL;
    qmp_guest_fsfreeze_thaw(&local_err);
    if (local_err) {
        g_debug("cleanup thaw: %s", error_get_pretty(local_err));
        error_free(local_err);
    }
    return 0;
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);

    return 0;
}

/*
 * Thaw local file systems using Volume Shadow-copy Service.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    int i;

    if (!vss_initialized()) {
        error_set(errp, QERR_UNSUPPORTED);
        return 0;
    }

    qga_vss_fsfreeze(&i, errp, false);

    ga_unset_frozen(ga_state);
    return i;
}

static void guest_fsfreeze_cleanup(void)
{
    Error *err = NULL;

    if (!vss_initialized()) {
        return;
    }

    if (ga_is_frozen(ga_state) == GUEST_FSFREEZE_STATUS_FROZEN) {
        qmp_guest_fsfreeze_thaw(&err);
        if (err) {
            slog("failed to clean up frozen filesystems: %s",
                 error_get_pretty(err));
            error_free(err);
        }
    }

    vss_deinit(true);
}

/*
 * Walk list of mounted file systems in the guest, and discard unused
 * areas.
 */
void qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

typedef enum {
    GUEST_SUSPEND_MODE_DISK,
    GUEST_SUSPEND_MODE_RAM
} GuestSuspendMode;

static void check_suspend_mode(GuestSuspendMode mode, Error **errp)
{
    SYSTEM_POWER_CAPABILITIES sys_pwr_caps;
    Error *local_err = NULL;

    ZeroMemory(&sys_pwr_caps, sizeof(sys_pwr_caps));
    if (!GetPwrCapabilities(&sys_pwr_caps)) {
        error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                  "failed to determine guest suspend capabilities");
        goto out;
    }

    switch (mode) {
    case GUEST_SUSPEND_MODE_DISK:
        if (!sys_pwr_caps.SystemS4) {
            error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                      "suspend-to-disk not supported by OS");
        }
        break;
    case GUEST_SUSPEND_MODE_RAM:
        if (!sys_pwr_caps.SystemS3) {
            error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                      "suspend-to-ram not supported by OS");
        }
        break;
    default:
        error_set(&local_err, QERR_INVALID_PARAMETER_VALUE, "mode",
                  "GuestSuspendMode");
    }

out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static DWORD WINAPI do_suspend(LPVOID opaque)
{
    GuestSuspendMode *mode = opaque;
    DWORD ret = 0;

    if (!SetSuspendState(*mode == GUEST_SUSPEND_MODE_DISK, TRUE, TRUE)) {
        slog("failed to suspend guest, %lu", GetLastError());
        ret = -1;
    }
    g_free(mode);
    return ret;
}

void qmp_guest_suspend_disk(Error **errp)
{
    Error *local_err = NULL;
    GuestSuspendMode *mode = g_malloc(sizeof(GuestSuspendMode));

    *mode = GUEST_SUSPEND_MODE_DISK;
    check_suspend_mode(*mode, &local_err);
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    execute_async(do_suspend, mode, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        g_free(mode);
    }
}

void qmp_guest_suspend_ram(Error **errp)
{
    Error *local_err = NULL;
    GuestSuspendMode *mode = g_malloc(sizeof(GuestSuspendMode));

    *mode = GUEST_SUSPEND_MODE_RAM;
    check_suspend_mode(*mode, &local_err);
    acquire_privilege(SE_SHUTDOWN_NAME, &local_err);
    execute_async(do_suspend, mode, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        g_free(mode);
    }
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

int64_t qmp_guest_get_time(Error **errp)
{
    SYSTEMTIME ts = {0};
    int64_t time_ns;
    FILETIME tf;

    GetSystemTime(&ts);
    if (ts.wYear < 1601 || ts.wYear > 30827) {
        error_setg(errp, "Failed to get time");
        return -1;
    }

    if (!SystemTimeToFileTime(&ts, &tf)) {
        error_setg(errp, "Failed to convert system time: %d", (int)GetLastError());
        return -1;
    }

    time_ns = ((((int64_t)tf.dwHighDateTime << 32) | tf.dwLowDateTime)
                - W32_FT_OFFSET) * 100;

    return time_ns;
}

void qmp_guest_set_time(bool has_time, int64_t time_ns, Error **errp)
{
    Error *local_err = NULL;
    SYSTEMTIME ts;
    FILETIME tf;
    LONGLONG time;

    if (has_time) {
        /* Okay, user passed a time to set. Validate it. */
        if (time_ns < 0 || time_ns / 100 > INT64_MAX - W32_FT_OFFSET) {
            error_setg(errp, "Time %" PRId64 "is invalid", time_ns);
            return;
        }

        time = time_ns / 100 + W32_FT_OFFSET;

        tf.dwLowDateTime = (DWORD) time;
        tf.dwHighDateTime = (DWORD) (time >> 32);

        if (!FileTimeToSystemTime(&tf, &ts)) {
            error_setg(errp, "Failed to convert system time %d",
                       (int)GetLastError());
            return;
        }
    } else {
        /* Otherwise read the time from RTC which contains the correct value.
         * Hopefully. */
        GetSystemTime(&ts);
        if (ts.wYear < 1601 || ts.wYear > 30827) {
            error_setg(errp, "Failed to get time");
            return;
        }
    }

    acquire_privilege(SE_SYSTEMTIME_NAME, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!SetSystemTime(&ts)) {
        error_setg(errp, "Failed to set time to guest: %d", (int)GetLastError());
        return;
    }
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return -1;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
    if (vss_init(true)) {
        ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
    }
}
