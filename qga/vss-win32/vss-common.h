/*
 * QEMU Guest Agent win32 VSS common declarations
 *
 * Copyright Hitachi Data Systems Corp. 2013
 *
 * Authors:
 *  Tomoki Sekiyama   <tomoki.sekiyama@hds.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VSS_COMMON_H
#define VSS_COMMON_H

#define __MIDL_user_allocate_free_DEFINED__
#include <windows.h>
#include <shlwapi.h>

/* Reduce warnings to include vss.h */

/* Ignore annotations for MS IDE */
#define __in  IN
#define __out OUT
#define __RPC_unique_pointer
#define __RPC_string
#define __RPC__deref_inout_opt
#define __RPC__out
#ifndef __RPC__out_ecount_part
#define __RPC__out_ecount_part(x, y)
#endif
#define _declspec(x)
#undef uuid
#define uuid(x)

/* Undef some duplicated error codes redefined in vss.h */
#undef VSS_E_BAD_STATE
#undef VSS_E_PROVIDER_NOT_REGISTERED
#undef VSS_E_PROVIDER_VETO
#undef VSS_E_OBJECT_NOT_FOUND
#undef VSS_E_VOLUME_NOT_SUPPORTED
#undef VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER
#undef VSS_E_OBJECT_ALREADY_EXISTS
#undef VSS_E_UNEXPECTED_PROVIDER_ERROR
#undef VSS_E_INVALID_XML_DOCUMENT
#undef VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED
#undef VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED

/*
 * VSS headers must be installed from Microsoft VSS SDK 7.2 available at:
 * http://www.microsoft.com/en-us/download/details.aspx?id=23490
 */
#include <inc/win2003/vss.h>
#include "vss-handles.h"

/* Macros to convert char definitions to wchar */
#define _L(a) L##a
#define L(a) _L(a)

const GUID g_gProviderId = { 0x3629d4ed, 0xee09, 0x4e0e,
    {0x9a, 0x5c, 0x6d, 0x8b, 0xa2, 0x87, 0x2a, 0xef} };
const GUID g_gProviderVersion = { 0x11ef8b15, 0xcac6, 0x40d6,
    {0x8d, 0x5c, 0x8f, 0xfc, 0x16, 0x3f, 0x24, 0xca} };

const CLSID CLSID_QGAVSSProvider = { 0x6e6a3492, 0x8d4d, 0x440c,
    {0x96, 0x19, 0x5e, 0x5d, 0x0c, 0xc3, 0x1c, 0xa8} };

const TCHAR g_szClsid[] = TEXT("{6E6A3492-8D4D-440C-9619-5E5D0CC31CA8}");
const TCHAR g_szProgid[] = TEXT("QGAVSSProvider");

/* Enums undefined in VSS SDK 7.2 but defined in newer Windows SDK */
enum __VSS_VOLUME_SNAPSHOT_ATTRIBUTES {
    VSS_VOLSNAP_ATTR_NO_AUTORECOVERY       = 0x00000002,
    VSS_VOLSNAP_ATTR_TXF_RECOVERY          = 0x02000000
};


/* COM pointer utility; call ->Release() when it goes out of scope */
template <class T>
class COMPointer {
    COMPointer(const COMPointer<T> &p) { } /* no copy */
    T *p;
public:
    COMPointer &operator=(T *new_p)
    {
        /* Assignment of a new T* (or NULL) causes release of previous p */
        if (p && p != new_p) {
            p->Release();
        }
        p = new_p;
        return *this;
    }
    /* Replace by assignment to the pointer of p  */
    T **replace(void)
    {
        *this = NULL;
        return &p;
    }
    /* Make COMPointer be used like T* */
    operator T*() { return p; }
    T *operator->(void) { return p; }
    T &operator*(void) { return *p; }
    operator bool() { return !!p; }

    COMPointer(T *p = NULL) : p(p) { }
    ~COMPointer() { *this = NULL; }  /* Automatic release */
};

/*
 * COM initializer; this should declared before COMPointer to uninitialize COM
 * after releasing COM objects.
 */
class COMInitializer {
public:
    COMInitializer() { CoInitialize(NULL); }
    ~COMInitializer() { CoUninitialize(); }
};

#endif
