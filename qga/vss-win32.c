/*
 * QEMU Guest Agent VSS utility functions
 *
 * Copyright Hitachi Data Systems Corp. 2013
 *
 * Authors:
 *  Tomoki Sekiyama   <tomoki.sekiyama@hds.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <windows.h>
#include "qga/guest-agent-core.h"
#include "qga/vss-win32.h"
#include "qga/vss-win32/requester.h"

#define QGA_VSS_DLL "qga-vss.dll"

static HMODULE provider_lib;

/* Call a function in qga-vss.dll with the specified name */
static HRESULT call_vss_provider_func(const char *func_name)
{
    FARPROC WINAPI func;

    g_assert(provider_lib);

    func = GetProcAddress(provider_lib, func_name);
    if (!func) {
        char *msg;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (char *)&msg, 0, NULL);
        fprintf(stderr, "failed to load %s from %s: %s",
                func_name, QGA_VSS_DLL, msg);
        LocalFree(msg);
        return E_FAIL;
    }

    return func();
}

/* Check whether this OS version supports VSS providers */
static bool vss_check_os_version(void)
{
    OSVERSIONINFO OSver;

    OSver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&OSver);
    if ((OSver.dwMajorVersion == 5 && OSver.dwMinorVersion >= 2) ||
       OSver.dwMajorVersion > 5) {
        BOOL wow64 = false;
#ifndef _WIN64
        /* Provider doesn't work under WOW64 (32bit agent on 64bit OS) */
        if (!IsWow64Process(GetCurrentProcess(), &wow64)) {
            fprintf(stderr, "failed to IsWow64Process (Error: %lx\n)\n",
                    GetLastError());
            return false;
        }
        if (wow64) {
            fprintf(stderr, "Warning: Running under WOW64\n");
        }
#endif
        return !wow64;
    }
    return false;
}

/* Load qga-vss.dll */
bool vss_init(bool init_requester)
{
    if (!vss_check_os_version()) {
        /* Do nothing if OS doesn't support providers. */
        fprintf(stderr, "VSS provider is not supported in this OS version: "
                "fsfreeze is disabled.\n");
        return false;
    }

    provider_lib = LoadLibraryA(QGA_VSS_DLL);
    if (!provider_lib) {
        char *msg;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (char *)&msg, 0, NULL);
        fprintf(stderr, "failed to load %s: %sfsfreeze is disabled\n",
                QGA_VSS_DLL, msg);
        LocalFree(msg);
        return false;
    }

    if (init_requester) {
        HRESULT hr = call_vss_provider_func("requester_init");
        if (FAILED(hr)) {
            fprintf(stderr, "fsfreeze is disabled.\n");
            vss_deinit(false);
            return false;
        }
    }

    return true;
}

/* Unload qga-provider.dll */
void vss_deinit(bool deinit_requester)
{
    if (deinit_requester) {
        call_vss_provider_func("requester_deinit");
    }
    FreeLibrary(provider_lib);
    provider_lib = NULL;
}

bool vss_initialized(void)
{
    return !!provider_lib;
}

int ga_install_vss_provider(void)
{
    HRESULT hr;

    if (!vss_init(false)) {
        fprintf(stderr, "Installation of VSS provider is skipped. "
                "fsfreeze will be disabled.\n");
        return 0;
    }
    hr = call_vss_provider_func("COMRegister");
    vss_deinit(false);

    return SUCCEEDED(hr) ? 0 : EXIT_FAILURE;
}

void ga_uninstall_vss_provider(void)
{
    if (!vss_init(false)) {
        fprintf(stderr, "Removal of VSS provider is skipped.\n");
        return;
    }
    call_vss_provider_func("COMUnregister");
    vss_deinit(false);
}

/* Call VSS requester and freeze/thaw filesystems and applications */
void qga_vss_fsfreeze(int *nr_volume, Error **errp, bool freeze)
{
    const char *func_name = freeze ? "requester_freeze" : "requester_thaw";
    QGAVSSRequesterFunc func;
    ErrorSet errset = {
        .error_setg_win32_wrapper = error_setg_win32_internal,
        .errp = errp,
    };

    g_assert(errp);             /* requester.cpp requires it */
    func = (QGAVSSRequesterFunc)GetProcAddress(provider_lib, func_name);
    if (!func) {
        error_setg_win32(errp, GetLastError(), "failed to load %s from %s",
                         func_name, QGA_VSS_DLL);
        return;
    }

    func(nr_volume, &errset);
}
