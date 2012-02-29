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
#include "qga-qmp-commands.h"
#include "qerror.h"

#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif

static void acquire_privilege(const char *name, Error **err)
{
    HANDLE token;
    TOKEN_PRIVILEGES priv;
    Error *local_err = NULL;

    if (error_is_set(err)) {
        return;
    }

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

        CloseHandle(token);
    } else {
        error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                  "failed to open privilege token");
    }

out:
    if (local_err) {
        error_propagate(err, local_err);
    }
}

static void execute_async(DWORD WINAPI (*func)(LPVOID), LPVOID opaque, Error **err)
{
    Error *local_err = NULL;

    if (error_is_set(err)) {
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, func, opaque, 0, NULL);
    if (!thread) {
        error_set(&local_err, QERR_QGA_COMMAND_FAILED,
                  "failed to dispatch asynchronous command");
        error_propagate(err, local_err);
    }
}

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **err)
{
    UINT shutdown_flag = EWX_FORCE;

    slog("guest-shutdown called, mode: %s", mode);

    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag |= EWX_POWEROFF;
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag |= EWX_SHUTDOWN;
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag |= EWX_REBOOT;
    } else {
        error_set(err, QERR_INVALID_PARAMETER_VALUE, "mode",
                  "halt|powerdown|reboot");
        return;
    }

    /* Request a shutdown privilege, but try to shut down the system
       anyway. */
    acquire_privilege(SE_SHUTDOWN_NAME, err);
    if (error_is_set(err)) {
        return;
    }

    if (!ExitWindowsEx(shutdown_flag, SHTDN_REASON_FLAG_PLANNED)) {
        slog("guest-shutdown failed: %d", GetLastError());
        error_set(err, QERR_UNDEFINED_ERROR);
    }
}

int64_t qmp_guest_file_open(const char *path, bool has_mode, const char *mode, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

void qmp_guest_file_close(int64_t handle, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                   int64_t count, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

GuestFileWrite *qmp_guest_file_write(int64_t handle, const char *buf_b64,
                                     bool has_count, int64_t count, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                   int64_t whence, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

void qmp_guest_file_flush(int64_t handle, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

/*
 * Walk list of mounted file systems in the guest, and freeze the ones which
 * are real local file systems.
 */
int64_t qmp_guest_fsfreeze_freeze(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

/*
 * Walk list of frozen file systems in the guest, and thaw them.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return 0;
}

typedef enum {
    GUEST_SUSPEND_MODE_DISK,
    GUEST_SUSPEND_MODE_RAM
} GuestSuspendMode;

static void check_suspend_mode(GuestSuspendMode mode, Error **err)
{
    SYSTEM_POWER_CAPABILITIES sys_pwr_caps;
    Error *local_err = NULL;

    if (error_is_set(err)) {
        return;
    }
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
        error_propagate(err, local_err);
    }
}

static DWORD WINAPI do_suspend(LPVOID opaque)
{
    GuestSuspendMode *mode = opaque;
    DWORD ret = 0;

    if (!SetSuspendState(*mode == GUEST_SUSPEND_MODE_DISK, TRUE, TRUE)) {
        slog("failed to suspend guest, %s", GetLastError());
        ret = -1;
    }
    g_free(mode);
    return ret;
}

void qmp_guest_suspend_disk(Error **err)
{
    GuestSuspendMode *mode = g_malloc(sizeof(GuestSuspendMode));

    *mode = GUEST_SUSPEND_MODE_DISK;
    check_suspend_mode(*mode, err);
    acquire_privilege(SE_SHUTDOWN_NAME, err);
    execute_async(do_suspend, mode, err);

    if (error_is_set(err)) {
        g_free(mode);
    }
}

void qmp_guest_suspend_ram(Error **err)
{
    GuestSuspendMode *mode = g_malloc(sizeof(GuestSuspendMode));

    *mode = GUEST_SUSPEND_MODE_RAM;
    check_suspend_mode(*mode, err);
    acquire_privilege(SE_SHUTDOWN_NAME, err);
    execute_async(do_suspend, mode, err);

    if (error_is_set(err)) {
        g_free(mode);
    }
}

void qmp_guest_suspend_hybrid(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
    return NULL;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
}
