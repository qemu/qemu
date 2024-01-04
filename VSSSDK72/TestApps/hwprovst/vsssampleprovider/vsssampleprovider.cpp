/*--

Copyright (C) Microsoft Corporation, 2003

Module Name:

    VssSampleProvider.cpp

Abstract:

    Implementation of COM DLL exports, global GUIDs and strings

Notes:

Revision History:

--*/

#include "stdafx.h"
#include "resource.h"
#include "VssSampleProvider.h"

// {B57190AF-454A-4dd0-8AFD-E57FACD5D9AF}
static const GUID g_gProviderId = 
    { 0xb57190af, 0x454a, 0x4dd0, { 0x8a, 0xfd, 0xe5, 0x7f, 0xac, 0xd5, 0xd9, 0xaf } };
// {90561D4F-0472-4fbc-B738-3D26341045F3}
static const GUID g_gProviderVersion = 
    { 0x90561d4f, 0x472, 0x4fbc, { 0xb7, 0x38, 0x3d, 0x26, 0x34, 0x10, 0x45, 0xf3 } };
static WCHAR* g_wszProviderName =   L"VSS Sample HW Provider";
static WCHAR* g_wszProviderVersion = L"1.0";

class CVssSampleProviderModule : public CAtlDllModuleT< CVssSampleProviderModule >
{
public :
    DECLARE_LIBID(LIBID_VssSampleProviderLib)
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_VSSSAMPLEPROVIDER, "{BAFB1857-FB9A-48C2-A5DB-D76F934D4E3F}")
};

CVssSampleProviderModule _AtlModule;


// DLL Entry Point
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    hInstance;
    return _AtlModule.DllMain(dwReason, lpReserved); 
}


// Used to determine whether the DLL can be unloaded by OLE
STDAPI DllCanUnloadNow(void)
{
    return _AtlModule.DllCanUnloadNow();
}


// Returns a class factory to create an object of the requested type
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}


// DllRegisterServer - Adds entries to the system registry
STDAPI DllRegisterServer(void)
{
    // registers object, typelib and all interfaces in typelib
    HRESULT hr = _AtlModule.DllRegisterServer();
    
    CComPtr<IVssAdmin> pVssAdmin;

    if (SUCCEEDED( hr )) {
        hr = CoCreateInstance( CLSID_VSSCoordinator,
            NULL,
            CLSCTX_ALL,
            IID_IVssAdmin,
            (void **) &pVssAdmin);
    }

    if (SUCCEEDED( hr )) {
        hr = pVssAdmin->RegisterProvider(g_gProviderId,
            CLSID_SampleProvider,
            g_wszProviderName,
            VSS_PROV_HARDWARE,
            g_wszProviderVersion,
            g_gProviderVersion );
    }
    
    pVssAdmin.Release();

    return hr;
}


// DllUnregisterServer - Removes entries from the system registry
STDAPI DllUnregisterServer(void)
{
    CComPtr<IVssAdmin> pVssAdmin;
    
    HRESULT hr = CoCreateInstance( CLSID_VSSCoordinator,
                           NULL,
                           CLSCTX_ALL,
                           IID_IVssAdmin,
                           (void **) &pVssAdmin);

    if (SUCCEEDED( hr )) {
        hr = pVssAdmin->UnregisterProvider( g_gProviderId );
    }
    
    hr = _AtlModule.DllUnregisterServer();

    pVssAdmin.Release();

    return hr;
}
