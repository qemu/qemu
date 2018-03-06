/*
 * QEMU Guest Agent win32 VSS Provider installer
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

#include "vss-common.h"
#include <inc/win2003/vscoordint.h>
#include "install.h"
#include <wbemidl.h>
#include <comdef.h>
#include <comutil.h>
#include <sddl.h>

#define BUFFER_SIZE 1024

extern HINSTANCE g_hinstDll;

const GUID CLSID_COMAdminCatalog = { 0xF618C514, 0xDFB8, 0x11d1,
    {0xA2, 0xCF, 0x00, 0x80, 0x5F, 0xC7, 0x92, 0x35} };
const GUID IID_ICOMAdminCatalog2 = { 0x790C6E0B, 0x9194, 0x4cc9,
    {0x94, 0x26, 0xA4, 0x8A, 0x63, 0x18, 0x56, 0x96} };
const GUID CLSID_WbemLocator = { 0x4590f811, 0x1d3a, 0x11d0,
    {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24} };
const GUID IID_IWbemLocator = { 0xdc12a687, 0x737f, 0x11cf,
    {0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24} };

void errmsg(DWORD err, const char *text)
{
    /*
     * `text' contains function call statement when errmsg is called via chk().
     * To make error message more readable, we cut off the text after '('.
     * If text doesn't contains '(', negative precision is given, which is
     * treated as though it were missing.
     */
    char *msg = NULL, *nul = strchr(text, '(');
    int len = nul ? nul - text : -1;

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (char *)&msg, 0, NULL);
    fprintf(stderr, "%.*s. (Error: %lx) %s\n", len, text, err, msg);
    LocalFree(msg);
}

static void errmsg_dialog(DWORD err, const char *text, const char *opt = "")
{
    char *msg, buf[512];

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (char *)&msg, 0, NULL);
    snprintf(buf, sizeof(buf), "%s%s. (Error: %lx) %s", text, opt, err, msg);
    MessageBox(NULL, buf, "Error from " QGA_PROVIDER_NAME, MB_OK|MB_ICONERROR);
    LocalFree(msg);
}

#define _chk(hr, status, msg, err_label)        \
    do {                                        \
        hr = (status);                          \
        if (FAILED(hr)) {                       \
            errmsg(hr, msg);                    \
            goto err_label;                     \
        }                                       \
    } while (0)

#define chk(status) _chk(hr, status, "Failed to " #status, out)

#if !defined(__MINGW64_VERSION_MAJOR) || !defined(__MINGW64_VERSION_MINOR) || \
    __MINGW64_VERSION_MAJOR * 100 + __MINGW64_VERSION_MINOR < 301
void __stdcall _com_issue_error(HRESULT hr)
{
    errmsg(hr, "Unexpected error in COM");
}
#endif

template<class T>
HRESULT put_Value(ICatalogObject *pObj, LPCWSTR name, T val)
{
    return pObj->put_Value(_bstr_t(name), _variant_t(val));
}

/* Lookup Administrators group name from winmgmt */
static HRESULT GetAdminName(_bstr_t *name)
{
    HRESULT hr;
    COMPointer<IWbemLocator> pLoc;
    COMPointer<IWbemServices> pSvc;
    COMPointer<IEnumWbemClassObject> pEnum;
    COMPointer<IWbemClassObject> pWobj;
    ULONG returned;
    _variant_t var;

    chk(CoCreateInstance(CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                         IID_IWbemLocator, (LPVOID *)pLoc.replace()));
    chk(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, NULL,
                            0, 0, 0, pSvc.replace()));
    chk(CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                          NULL, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE));
    chk(pSvc->ExecQuery(_bstr_t(L"WQL"),
                        _bstr_t(L"select * from Win32_Account where "
                                "SID='S-1-5-32-544' and localAccount=TRUE"),
                        WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY,
                        NULL, pEnum.replace()));
    if (!pEnum) {
        hr = E_FAIL;
        errmsg(hr, "Failed to query for Administrators");
        goto out;
    }
    chk(pEnum->Next(WBEM_INFINITE, 1, pWobj.replace(), &returned));
    if (returned == 0) {
        hr = E_FAIL;
        errmsg(hr, "No Administrators found");
        goto out;
    }

    chk(pWobj->Get(_bstr_t(L"Name"), 0, &var, 0, 0));
    try {
        *name = var;
    } catch(...) {
        hr = E_FAIL;
        errmsg(hr, "Failed to get name of Administrators");
        goto out;
    }

out:
    return hr;
}

/* Acquire group or user name by SID */
static HRESULT getNameByStringSID(
    const wchar_t *sid, LPWSTR buffer, LPDWORD bufferLen)
{
    HRESULT hr = S_OK;
    PSID psid = NULL;
    SID_NAME_USE groupType;
    DWORD domainNameLen = BUFFER_SIZE;
    wchar_t domainName[BUFFER_SIZE];

    if (!ConvertStringSidToSidW(sid, &psid)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto out;
    }
    if (!LookupAccountSidW(NULL, psid, buffer, bufferLen,
                           domainName, &domainNameLen, &groupType)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        /* Fall through and free psid */
    }

    LocalFree(psid);

out:
    return hr;
}

/* Find and iterate QGA VSS provider in COM+ Application Catalog */
static HRESULT QGAProviderFind(
    HRESULT (*found)(ICatalogCollection *, int, void *), void *arg)
{
    HRESULT hr;
    COMInitializer initializer;
    COMPointer<IUnknown> pUnknown;
    COMPointer<ICOMAdminCatalog2> pCatalog;
    COMPointer<ICatalogCollection> pColl;
    COMPointer<ICatalogObject> pObj;
    _variant_t var;
    long i, n;

    chk(CoCreateInstance(CLSID_COMAdminCatalog, NULL, CLSCTX_INPROC_SERVER,
                         IID_IUnknown, (void **)pUnknown.replace()));
    chk(pUnknown->QueryInterface(IID_ICOMAdminCatalog2,
                                 (void **)pCatalog.replace()));
    chk(pCatalog->GetCollection(_bstr_t(L"Applications"),
                                (IDispatch **)pColl.replace()));
    chk(pColl->Populate());

    chk(pColl->get_Count(&n));
    for (i = n - 1; i >= 0; i--) {
        chk(pColl->get_Item(i, (IDispatch **)pObj.replace()));
        chk(pObj->get_Value(_bstr_t(L"Name"), &var));
        if (var == _variant_t(QGA_PROVIDER_LNAME)) {
            if (FAILED(found(pColl, i, arg))) {
                goto out;
            }
        }
    }
    chk(pColl->SaveChanges(&n));

out:
    return hr;
}

/* Count QGA VSS provider in COM+ Application Catalog */
static HRESULT QGAProviderCount(ICatalogCollection *coll, int i, void *arg)
{
    (*(int *)arg)++;
    return S_OK;
}

/* Remove QGA VSS provider from COM+ Application Catalog Collection */
static HRESULT QGAProviderRemove(ICatalogCollection *coll, int i, void *arg)
{
    HRESULT hr;

    fprintf(stderr, "Removing COM+ Application: %s\n", QGA_PROVIDER_NAME);
    chk(coll->Remove(i));
out:
    return hr;
}

/* Unregister this module from COM+ Applications Catalog */
STDAPI COMUnregister(void)
{
    HRESULT hr;

    DllUnregisterServer();
    chk(QGAProviderFind(QGAProviderRemove, NULL));
out:
    return hr;
}

/* Register this module to COM+ Applications Catalog */
STDAPI COMRegister(void)
{
    HRESULT hr;
    COMInitializer initializer;
    COMPointer<IUnknown> pUnknown;
    COMPointer<ICOMAdminCatalog2> pCatalog;
    COMPointer<ICatalogCollection> pApps, pRoles, pUsersInRole;
    COMPointer<ICatalogObject> pObj;
    long n;
    _bstr_t name;
    _variant_t key;
    CHAR dllPath[MAX_PATH], tlbPath[MAX_PATH];
    bool unregisterOnFailure = false;
    int count = 0;
    DWORD bufferLen = BUFFER_SIZE;
    wchar_t buffer[BUFFER_SIZE];
    const wchar_t *administratorsGroupSID = L"S-1-5-32-544";
    const wchar_t *systemUserSID = L"S-1-5-18";

    if (!g_hinstDll) {
        errmsg(E_FAIL, "Failed to initialize DLL");
        return E_FAIL;
    }

    chk(QGAProviderFind(QGAProviderCount, (void *)&count));
    if (count) {
        errmsg(E_ABORT, "QGA VSS Provider is already installed");
        return E_ABORT;
    }

    chk(CoCreateInstance(CLSID_COMAdminCatalog, NULL, CLSCTX_INPROC_SERVER,
                         IID_IUnknown, (void **)pUnknown.replace()));
    chk(pUnknown->QueryInterface(IID_ICOMAdminCatalog2,
                                 (void **)pCatalog.replace()));

    /* Install COM+ Component */

    chk(pCatalog->GetCollection(_bstr_t(L"Applications"),
                                (IDispatch **)pApps.replace()));
    chk(pApps->Populate());
    chk(pApps->Add((IDispatch **)&pObj));
    chk(put_Value(pObj, L"Name",        QGA_PROVIDER_LNAME));
    chk(put_Value(pObj, L"Description", QGA_PROVIDER_LNAME));
    chk(put_Value(pObj, L"ApplicationAccessChecksEnabled", true));
    chk(put_Value(pObj, L"Authentication",                 short(6)));
    chk(put_Value(pObj, L"AuthenticationCapability",       short(2)));
    chk(put_Value(pObj, L"ImpersonationLevel",             short(2)));
    chk(pApps->SaveChanges(&n));

    /* The app should be deleted if something fails after SaveChanges */
    unregisterOnFailure = true;

    chk(pObj->get_Key(&key));

    if (!GetModuleFileName(g_hinstDll, dllPath, sizeof(dllPath))) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        errmsg(hr, "GetModuleFileName failed");
        goto out;
    }
    n = strlen(dllPath);
    if (n < 3) {
        hr = E_FAIL;
        errmsg(hr, "Failed to lookup dll");
        goto out;
    }
    strcpy(tlbPath, dllPath);
    strcpy(tlbPath+n-3, "tlb");
    fprintf(stderr, "Registering " QGA_PROVIDER_NAME ":\n");
    fprintf(stderr, "  %s\n", dllPath);
    fprintf(stderr, "  %s\n", tlbPath);
    if (!PathFileExists(tlbPath)) {
        hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        errmsg(hr, "Failed to lookup tlb");
        goto out;
    }

    chk(pCatalog->CreateServiceForApplication(
            _bstr_t(QGA_PROVIDER_LNAME), _bstr_t(QGA_PROVIDER_LNAME),
            _bstr_t(L"SERVICE_DEMAND_START"), _bstr_t(L"SERVICE_ERROR_NORMAL"),
            _bstr_t(L""), _bstr_t(L".\\localsystem"), _bstr_t(L""), FALSE));
    chk(pCatalog->InstallComponent(_bstr_t(QGA_PROVIDER_LNAME),
                                   _bstr_t(dllPath), _bstr_t(tlbPath),
                                   _bstr_t("")));

    /* Setup roles of the applicaion */

    chk(getNameByStringSID(administratorsGroupSID, buffer, &bufferLen));
    chk(pApps->GetCollection(_bstr_t(L"Roles"), key,
                             (IDispatch **)pRoles.replace()));
    chk(pRoles->Populate());
    chk(pRoles->Add((IDispatch **)pObj.replace()));
    chk(put_Value(pObj, L"Name", buffer));
    chk(put_Value(pObj, L"Description", L"Administrators group"));
    chk(pRoles->SaveChanges(&n));
    chk(pObj->get_Key(&key));

    /* Setup users in the role */

    chk(pRoles->GetCollection(_bstr_t(L"UsersInRole"), key,
                              (IDispatch **)pUsersInRole.replace()));
    chk(pUsersInRole->Populate());

    chk(pUsersInRole->Add((IDispatch **)pObj.replace()));
    chk(GetAdminName(&name));
    chk(put_Value(pObj, L"User", _bstr_t(".\\") + name));

    bufferLen = BUFFER_SIZE;
    chk(getNameByStringSID(systemUserSID, buffer, &bufferLen));
    chk(pUsersInRole->Add((IDispatch **)pObj.replace()));
    chk(put_Value(pObj, L"User", buffer));
    chk(pUsersInRole->SaveChanges(&n));

out:
    if (unregisterOnFailure && FAILED(hr)) {
        COMUnregister();
    }

    return hr;
}


static BOOL CreateRegistryKey(LPCTSTR key, LPCTSTR value, LPCTSTR data)
{
    HKEY  hKey;
    LONG  ret;
    DWORD size;

    ret = RegCreateKeyEx(HKEY_CLASSES_ROOT, key, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (ret != ERROR_SUCCESS) {
        goto out;
    }

    if (data != NULL) {
        size = strlen(data) + 1;
    } else {
        size = 0;
    }

    ret = RegSetValueEx(hKey, value, 0, REG_SZ, (LPBYTE)data, size);
    RegCloseKey(hKey);

out:
    if (ret != ERROR_SUCCESS) {
        /* As we cannot printf within DllRegisterServer(), show a dialog. */
        errmsg_dialog(ret, "Cannot add registry", key);
        return FALSE;
    }
    return TRUE;
}

/* Register this dll as a VSS provider */
STDAPI DllRegisterServer(void)
{
    COMInitializer initializer;
    COMPointer<IVssAdmin> pVssAdmin;
    HRESULT hr = E_FAIL;
    char dllPath[MAX_PATH];
    char key[256];

    if (!g_hinstDll) {
        errmsg_dialog(hr, "Module instance is not available");
        goto out;
    }

    /* Add this module to registery */

    sprintf(key, "CLSID\\%s", g_szClsid);
    if (!CreateRegistryKey(key, NULL, g_szClsid)) {
        goto out;
    }

    if (!GetModuleFileName(g_hinstDll, dllPath, sizeof(dllPath))) {
        errmsg_dialog(GetLastError(), "GetModuleFileName failed");
        goto out;
    }

    sprintf(key, "CLSID\\%s\\InprocServer32", g_szClsid);
    if (!CreateRegistryKey(key, NULL, dllPath)) {
        goto out;
    }

    if (!CreateRegistryKey(key, "ThreadingModel", "Apartment")) {
        goto out;
    }

    sprintf(key, "CLSID\\%s\\ProgID", g_szClsid);
    if (!CreateRegistryKey(key, NULL, g_szProgid)) {
        goto out;
    }

    if (!CreateRegistryKey(g_szProgid, NULL, QGA_PROVIDER_NAME)) {
        goto out;
    }

    sprintf(key, "%s\\CLSID", g_szProgid);
    if (!CreateRegistryKey(key, NULL, g_szClsid)) {
        goto out;
    }

    hr = CoCreateInstance(CLSID_VSSCoordinator, NULL, CLSCTX_ALL,
                          IID_IVssAdmin, (void **)pVssAdmin.replace());
    if (FAILED(hr)) {
        errmsg_dialog(hr, "CoCreateInstance(VSSCoordinator) failed");
        goto out;
    }

    hr = pVssAdmin->RegisterProvider(g_gProviderId, CLSID_QGAVSSProvider,
                                     const_cast<WCHAR*>(QGA_PROVIDER_LNAME),
                                     VSS_PROV_SOFTWARE,
                                     const_cast<WCHAR*>(QGA_PROVIDER_VERSION),
                                     g_gProviderVersion);
    if (FAILED(hr)) {
        errmsg_dialog(hr, "RegisterProvider failed");
    }

out:
    if (FAILED(hr)) {
        DllUnregisterServer();
    }

    return hr;
}

/* Unregister this VSS hardware provider from the system */
STDAPI DllUnregisterServer(void)
{
    TCHAR key[256];
    COMInitializer initializer;
    COMPointer<IVssAdmin> pVssAdmin;

    HRESULT hr = CoCreateInstance(CLSID_VSSCoordinator,
                                  NULL, CLSCTX_ALL, IID_IVssAdmin,
                                  (void **)pVssAdmin.replace());
    if (SUCCEEDED(hr)) {
        hr = pVssAdmin->UnregisterProvider(g_gProviderId);
    } else {
        errmsg(hr, "CoCreateInstance(VSSCoordinator) failed");
    }

    sprintf(key, "CLSID\\%s", g_szClsid);
    SHDeleteKey(HKEY_CLASSES_ROOT, key);
    SHDeleteKey(HKEY_CLASSES_ROOT, g_szProgid);

    return S_OK; /* Uninstall should never fail */
}


/* Support function to convert ASCII string into BSTR (used in _bstr_t) */
namespace _com_util
{
    BSTR WINAPI ConvertStringToBSTR(const char *ascii) {
        int len = strlen(ascii);
        BSTR bstr = SysAllocStringLen(NULL, len);

        if (!bstr) {
            return NULL;
        }

        if (mbstowcs(bstr, ascii, len) == (size_t)-1) {
            fprintf(stderr, "Failed to convert string '%s' into BSTR", ascii);
            bstr[0] = 0;
        }
        return bstr;
    }
}

/* Stop QGA VSS provider service from COM+ Application Admin Catalog */

STDAPI StopService(void)
{
    HRESULT hr;
    COMInitializer initializer;
    COMPointer<IUnknown> pUnknown;
    COMPointer<ICOMAdminCatalog2> pCatalog;

    int count = 0;

    chk(QGAProviderFind(QGAProviderCount, (void *)&count));
    if (count) {
        chk(CoCreateInstance(CLSID_COMAdminCatalog, NULL, CLSCTX_INPROC_SERVER,
            IID_IUnknown, (void **)pUnknown.replace()));
        chk(pUnknown->QueryInterface(IID_ICOMAdminCatalog2,
            (void **)pCatalog.replace()));
        chk(pCatalog->ShutdownApplication(_bstr_t(QGA_PROVIDER_LNAME)));
    }

out:
    return hr;
}
