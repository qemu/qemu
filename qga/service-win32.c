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
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <windows.h>
#include "qga/service-win32.h"

static int printf_win_error(const char *text)
{
    DWORD err = GetLastError();
    char *message;
    int n;

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char *)&message, 0,
        NULL);
    n = printf("%s. (Error: %d) %s", text, err, message);
    LocalFree(message);

    return n;
}

int ga_install_service(const char *path, const char *logfile)
{
    SC_HANDLE manager;
    SC_HANDLE service;
    TCHAR cmdline[MAX_PATH];

    if (GetModuleFileName(NULL, cmdline, MAX_PATH) == 0) {
        printf_win_error("No full path to service's executable");
        return EXIT_FAILURE;
    }

    _snprintf(cmdline, MAX_PATH - strlen(cmdline), "%s -d", cmdline);

    if (path) {
        _snprintf(cmdline, MAX_PATH - strlen(cmdline), "%s -p %s", cmdline, path);
    }
    if (logfile) {
        _snprintf(cmdline, MAX_PATH - strlen(cmdline), "%s -l %s -v",
            cmdline, logfile);
    }

    g_debug("service's cmdline: %s", cmdline);

    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL) {
        printf_win_error("No handle to service control manager");
        return EXIT_FAILURE;
    }

    service = CreateService(manager, QGA_SERVICE_NAME, QGA_SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, cmdline, NULL, NULL, NULL, NULL, NULL);

    if (service) {
        SERVICE_DESCRIPTION desc = { (char *)QGA_SERVICE_DESCRIPTION };
        ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &desc);

        printf("Service was installed successfully.\n");
    } else {
        printf_win_error("Failed to install service");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    return (service == NULL);
}

int ga_uninstall_service(void)
{
    SC_HANDLE manager;
    SC_HANDLE service;

    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL) {
        printf_win_error("No handle to service control manager");
        return EXIT_FAILURE;
    }

    service = OpenService(manager, QGA_SERVICE_NAME, DELETE);
    if (service == NULL) {
        printf_win_error("No handle to service");
        CloseServiceHandle(manager);
        return EXIT_FAILURE;
    }

    if (DeleteService(service) == FALSE) {
        printf_win_error("Failed to delete service");
    } else {
        printf("Service was deleted successfully.\n");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    return EXIT_SUCCESS;
}
