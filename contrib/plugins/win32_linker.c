/*
 * Copyright (C) 2023, Greg Manning <gmanning@rapitasystems.com>
 *
 * This hook, __pfnDliFailureHook2, is documented in the microsoft documentation here:
 * https://learn.microsoft.com/en-us/cpp/build/reference/error-handling-and-notification
 * It gets called when a delay-loaded DLL encounters various errors.
 * We handle the specific case of a DLL looking for a "qemu.exe",
 * and give it the running executable (regardless of what it is named).
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <windows.h>
#include <delayimp.h>

FARPROC WINAPI dll_failure_hook(unsigned dliNotify, PDelayLoadInfo pdli);


PfnDliHook __pfnDliFailureHook2 = dll_failure_hook;

FARPROC WINAPI dll_failure_hook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliFailLoadLib) {
        /* If the failing request was for qemu.exe, ... */
        if (strcmp(pdli->szDll, "qemu.exe") == 0) {
            /* Then pass back a pointer to the top level module. */
            HMODULE top = GetModuleHandle(NULL);
            return (FARPROC) top;
        }
    }
    /* Otherwise we can't do anything special. */
    return 0;
}

