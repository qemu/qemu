/*
 * QEMU Guest Agent helpers for win32 service management
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Gal Hammer        <ghammer@redhat.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QGA_SERVICE_WIN32_H
#define QGA_SERVICE_WIN32_H

#include <windows.h>

#define QGA_SERVICE_DISPLAY_NAME "QEMU Guest Agent"
#define QGA_SERVICE_NAME         "qemu-ga"
#define QGA_SERVICE_DESCRIPTION  "Enables integration with QEMU machine emulator and virtualizer."

static const GUID GUID_VIOSERIAL_PORT = { 0x6fde7521, 0x1b65, 0x48ae,
{ 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26 } };

typedef struct GAService {
    SERVICE_STATUS status;
    SERVICE_STATUS_HANDLE status_handle;
    HDEVNOTIFY device_notification_handle;
} GAService;

int ga_install_service(const char *path, const char *logfile,
                       const char *state_dir);
int ga_uninstall_service(void);

#endif
