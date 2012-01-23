/*
 * QEMU Guest Agent win32-specific command implementations
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include "qga/guest-agent-core.h"
#include "qga-qmp-commands.h"
#include "qerror.h"

#ifndef SHTDN_REASON_FLAG_PLANNED
#define SHTDN_REASON_FLAG_PLANNED 0x80000000
#endif

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **err)
{
    HANDLE token;
    TOKEN_PRIVILEGES priv;
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
    if (OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token))
    {
        LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME,
            &priv.Privileges[0].Luid);

        priv.PrivilegeCount = 1;
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, FALSE, &priv, 0, NULL, 0);
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

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
}
