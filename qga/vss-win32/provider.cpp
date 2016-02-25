/*
 * QEMU Guest Agent win32 VSS Provider implementations
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
#include "inc/win2003/vscoordint.h"
#include "inc/win2003/vsprov.h"

#define VSS_TIMEOUT_MSEC (60*1000)

static long g_nComObjsInUse;
HINSTANCE g_hinstDll;

/* VSS common GUID's */

const CLSID CLSID_VSSCoordinator = { 0xE579AB5F, 0x1CC4, 0x44b4,
    {0xBE, 0xD9, 0xDE, 0x09, 0x91, 0xFF, 0x06, 0x23} };
const IID IID_IVssAdmin = { 0x77ED5996, 0x2F63, 0x11d3,
    {0x8A, 0x39, 0x00, 0xC0, 0x4F, 0x72, 0xD8, 0xE3} };

const IID IID_IVssHardwareSnapshotProvider = { 0x9593A157, 0x44E9, 0x4344,
    {0xBB, 0xEB, 0x44, 0xFB, 0xF9, 0xB0, 0x6B, 0x10} };
const IID IID_IVssSoftwareSnapshotProvider = { 0x609e123e, 0x2c5a, 0x44d3,
    {0x8f, 0x01, 0x0b, 0x1d, 0x9a, 0x47, 0xd1, 0xff} };
const IID IID_IVssProviderCreateSnapshotSet = { 0x5F894E5B, 0x1E39, 0x4778,
    {0x8E, 0x23, 0x9A, 0xBA, 0xD9, 0xF0, 0xE0, 0x8C} };
const IID IID_IVssProviderNotifications = { 0xE561901F, 0x03A5, 0x4afe,
    {0x86, 0xD0, 0x72, 0xBA, 0xEE, 0xCE, 0x70, 0x04} };

const IID IID_IVssEnumObject = { 0xAE1C7110, 0x2F60, 0x11d3,
    {0x8A, 0x39, 0x00, 0xC0, 0x4F, 0x72, 0xD8, 0xE3} };


void LockModule(BOOL lock)
{
    if (lock) {
        InterlockedIncrement(&g_nComObjsInUse);
    } else {
        InterlockedDecrement(&g_nComObjsInUse);
    }
}

/* Empty enumerator for VssObject */

class CQGAVSSEnumObject : public IVssEnumObject
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppObj);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    /* IVssEnumObject Methods */
    STDMETHODIMP Next(
        ULONG celt, VSS_OBJECT_PROP *rgelt, ULONG *pceltFetched);
    STDMETHODIMP Skip(ULONG celt);
    STDMETHODIMP Reset(void);
    STDMETHODIMP Clone(IVssEnumObject **ppenum);

    /* CQGAVSSEnumObject Methods */
    CQGAVSSEnumObject();
    ~CQGAVSSEnumObject();

private:
    long m_nRefCount;
};

CQGAVSSEnumObject::CQGAVSSEnumObject()
{
    m_nRefCount = 0;
    LockModule(TRUE);
}

CQGAVSSEnumObject::~CQGAVSSEnumObject()
{
    LockModule(FALSE);
}

STDMETHODIMP CQGAVSSEnumObject::QueryInterface(REFIID riid, void **ppObj)
{
    if (riid == IID_IUnknown || riid == IID_IVssEnumObject) {
        *ppObj = static_cast<void*>(static_cast<IVssEnumObject*>(this));
        AddRef();
        return S_OK;
    }
    *ppObj = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CQGAVSSEnumObject::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CQGAVSSEnumObject::Release()
{
    long nRefCount = InterlockedDecrement(&m_nRefCount);
    if (m_nRefCount == 0) {
        delete this;
    }
    return nRefCount;
}

STDMETHODIMP CQGAVSSEnumObject::Next(
    ULONG celt, VSS_OBJECT_PROP *rgelt, ULONG *pceltFetched)
{
    *pceltFetched = 0;
    return S_FALSE;
}

STDMETHODIMP CQGAVSSEnumObject::Skip(ULONG celt)
{
    return S_FALSE;
}

STDMETHODIMP CQGAVSSEnumObject::Reset(void)
{
    return S_OK;
}

STDMETHODIMP CQGAVSSEnumObject::Clone(IVssEnumObject **ppenum)
{
    return E_NOTIMPL;
}


/* QGAVssProvider */

class CQGAVssProvider :
    public IVssSoftwareSnapshotProvider,
    public IVssProviderCreateSnapshotSet,
    public IVssProviderNotifications
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppObj);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    /* IVssSoftwareSnapshotProvider Methods */
    STDMETHODIMP SetContext(LONG lContext);
    STDMETHODIMP GetSnapshotProperties(
        VSS_ID SnapshotId, VSS_SNAPSHOT_PROP *pProp);
    STDMETHODIMP Query(
        VSS_ID QueriedObjectId, VSS_OBJECT_TYPE eQueriedObjectType,
        VSS_OBJECT_TYPE eReturnedObjectsType, IVssEnumObject **ppEnum);
    STDMETHODIMP DeleteSnapshots(
        VSS_ID SourceObjectId, VSS_OBJECT_TYPE eSourceObjectType,
        BOOL bForceDelete, LONG *plDeletedSnapshots,
        VSS_ID *pNondeletedSnapshotID);
    STDMETHODIMP BeginPrepareSnapshot(
        VSS_ID SnapshotSetId, VSS_ID SnapshotId,
        VSS_PWSZ pwszVolumeName, LONG lNewContext);
    STDMETHODIMP IsVolumeSupported(
        VSS_PWSZ pwszVolumeName, BOOL *pbSupportedByThisProvider);
    STDMETHODIMP IsVolumeSnapshotted(
        VSS_PWSZ pwszVolumeName, BOOL *pbSnapshotsPresent,
        LONG *plSnapshotCompatibility);
    STDMETHODIMP SetSnapshotProperty(
        VSS_ID SnapshotId, VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId,
        VARIANT vProperty);
    STDMETHODIMP RevertToSnapshot(VSS_ID SnapshotId);
    STDMETHODIMP QueryRevertStatus(VSS_PWSZ pwszVolume, IVssAsync **ppAsync);

    /* IVssProviderCreateSnapshotSet Methods */
    STDMETHODIMP EndPrepareSnapshots(VSS_ID SnapshotSetId);
    STDMETHODIMP PreCommitSnapshots(VSS_ID SnapshotSetId);
    STDMETHODIMP CommitSnapshots(VSS_ID SnapshotSetId);
    STDMETHODIMP PostCommitSnapshots(
        VSS_ID SnapshotSetId, LONG lSnapshotsCount);
    STDMETHODIMP PreFinalCommitSnapshots(VSS_ID SnapshotSetId);
    STDMETHODIMP PostFinalCommitSnapshots(VSS_ID SnapshotSetId);
    STDMETHODIMP AbortSnapshots(VSS_ID SnapshotSetId);

    /* IVssProviderNotifications Methods */
    STDMETHODIMP OnLoad(IUnknown *pCallback);
    STDMETHODIMP OnUnload(BOOL bForceUnload);

    /* CQGAVssProvider Methods */
    CQGAVssProvider();
    ~CQGAVssProvider();

private:
    long m_nRefCount;
};

CQGAVssProvider::CQGAVssProvider()
{
    m_nRefCount = 0;
    LockModule(TRUE);
}

CQGAVssProvider::~CQGAVssProvider()
{
    LockModule(FALSE);
}

STDMETHODIMP CQGAVssProvider::QueryInterface(REFIID riid, void **ppObj)
{
    if (riid == IID_IUnknown) {
        *ppObj = static_cast<void*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IVssSoftwareSnapshotProvider) {
        *ppObj = static_cast<void*>(
            static_cast<IVssSoftwareSnapshotProvider*>(this));
        AddRef();
        return S_OK;
    }
    if (riid == IID_IVssProviderCreateSnapshotSet) {
        *ppObj = static_cast<void*>(
            static_cast<IVssProviderCreateSnapshotSet*>(this));
        AddRef();
        return S_OK;
    }
    if (riid == IID_IVssProviderNotifications) {
        *ppObj = static_cast<void*>(
            static_cast<IVssProviderNotifications*>(this));
        AddRef();
        return S_OK;
    }
    *ppObj = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CQGAVssProvider::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CQGAVssProvider::Release()
{
    long nRefCount = InterlockedDecrement(&m_nRefCount);
    if (m_nRefCount == 0) {
        delete this;
    }
    return nRefCount;
}


/*
 * IVssSoftwareSnapshotProvider methods
 */

STDMETHODIMP CQGAVssProvider::SetContext(LONG lContext)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::GetSnapshotProperties(
    VSS_ID SnapshotId, VSS_SNAPSHOT_PROP *pProp)
{
    return VSS_E_OBJECT_NOT_FOUND;
}

STDMETHODIMP CQGAVssProvider::Query(
    VSS_ID QueriedObjectId, VSS_OBJECT_TYPE eQueriedObjectType,
    VSS_OBJECT_TYPE eReturnedObjectsType, IVssEnumObject **ppEnum)
{
    try {
        *ppEnum = new CQGAVSSEnumObject;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    (*ppEnum)->AddRef();
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::DeleteSnapshots(
    VSS_ID SourceObjectId, VSS_OBJECT_TYPE eSourceObjectType,
    BOOL bForceDelete, LONG *plDeletedSnapshots, VSS_ID *pNondeletedSnapshotID)
{
    *plDeletedSnapshots = 0;
    *pNondeletedSnapshotID = SourceObjectId;
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::BeginPrepareSnapshot(
    VSS_ID SnapshotSetId, VSS_ID SnapshotId,
    VSS_PWSZ pwszVolumeName, LONG lNewContext)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::IsVolumeSupported(
    VSS_PWSZ pwszVolumeName, BOOL *pbSupportedByThisProvider)
{
    HANDLE hEventFrozen;

    /* Check if a requester is qemu-ga by whether an event is created */
    hEventFrozen = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_NAME_FROZEN);
    if (!hEventFrozen) {
        *pbSupportedByThisProvider = FALSE;
        return S_OK;
    }
    CloseHandle(hEventFrozen);

    *pbSupportedByThisProvider = TRUE;
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::IsVolumeSnapshotted(VSS_PWSZ pwszVolumeName,
    BOOL *pbSnapshotsPresent, LONG *plSnapshotCompatibility)
{
    *pbSnapshotsPresent = FALSE;
    *plSnapshotCompatibility = 0;
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::SetSnapshotProperty(VSS_ID SnapshotId,
    VSS_SNAPSHOT_PROPERTY_ID eSnapshotPropertyId, VARIANT vProperty)
{
    return E_NOTIMPL;
}

STDMETHODIMP CQGAVssProvider::RevertToSnapshot(VSS_ID SnapshotId)
{
    return E_NOTIMPL;
}

STDMETHODIMP CQGAVssProvider::QueryRevertStatus(
    VSS_PWSZ pwszVolume, IVssAsync **ppAsync)
{
    return E_NOTIMPL;
}


/*
 * IVssProviderCreateSnapshotSet methods
 */

STDMETHODIMP CQGAVssProvider::EndPrepareSnapshots(VSS_ID SnapshotSetId)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::PreCommitSnapshots(VSS_ID SnapshotSetId)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::CommitSnapshots(VSS_ID SnapshotSetId)
{
    HRESULT hr = S_OK;
    HANDLE hEventFrozen, hEventThaw, hEventTimeout;

    hEventFrozen = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_NAME_FROZEN);
    if (!hEventFrozen) {
        return E_FAIL;
    }

    hEventThaw = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_NAME_THAW);
    if (!hEventThaw) {
        CloseHandle(hEventFrozen);
        return E_FAIL;
    }

    hEventTimeout = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVENT_NAME_TIMEOUT);
    if (!hEventTimeout) {
        CloseHandle(hEventFrozen);
        CloseHandle(hEventThaw);
        return E_FAIL;
    }

    /* Send event to qemu-ga to notify filesystem is frozen */
    SetEvent(hEventFrozen);

    /* Wait until the snapshot is taken by the host. */
    if (WaitForSingleObject(hEventThaw, VSS_TIMEOUT_MSEC) != WAIT_OBJECT_0) {
        /* Send event to qemu-ga to notify the provider is timed out */
        SetEvent(hEventTimeout);
        hr = E_ABORT;
    }

    CloseHandle(hEventThaw);
    CloseHandle(hEventFrozen);
    CloseHandle(hEventTimeout);
    return hr;
}

STDMETHODIMP CQGAVssProvider::PostCommitSnapshots(
    VSS_ID SnapshotSetId, LONG lSnapshotsCount)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::PreFinalCommitSnapshots(VSS_ID SnapshotSetId)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::PostFinalCommitSnapshots(VSS_ID SnapshotSetId)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::AbortSnapshots(VSS_ID SnapshotSetId)
{
    return S_OK;
}

/*
 * IVssProviderNotifications methods
 */

STDMETHODIMP CQGAVssProvider::OnLoad(IUnknown *pCallback)
{
    return S_OK;
}

STDMETHODIMP CQGAVssProvider::OnUnload(BOOL bForceUnload)
{
    return S_OK;
}


/*
 * CQGAVssProviderFactory class
 */

class CQGAVssProviderFactory : public IClassFactory
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP CreateInstance(
        IUnknown *pUnknownOuter, REFIID iid, void **ppv);
    STDMETHODIMP LockServer(BOOL lock) { return E_NOTIMPL; }

    CQGAVssProviderFactory();
    ~CQGAVssProviderFactory();

private:
    long m_nRefCount;
};

CQGAVssProviderFactory::CQGAVssProviderFactory()
{
    m_nRefCount = 0;
    LockModule(TRUE);
}

CQGAVssProviderFactory::~CQGAVssProviderFactory()
{
    LockModule(FALSE);
}

STDMETHODIMP CQGAVssProviderFactory::QueryInterface(REFIID riid, void **ppv)
{
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<void*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CQGAVssProviderFactory::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CQGAVssProviderFactory::Release()
{
    long nRefCount = InterlockedDecrement(&m_nRefCount);
    if (m_nRefCount == 0) {
        delete this;
    }
    return nRefCount;
}

STDMETHODIMP CQGAVssProviderFactory::CreateInstance(
    IUnknown *pUnknownOuter, REFIID iid, void **ppv)
{
    CQGAVssProvider *pObj;

    if (pUnknownOuter) {
        return CLASS_E_NOAGGREGATION;
    }
    try {
        pObj = new CQGAVssProvider;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    HRESULT hr = pObj->QueryInterface(iid, ppv);
    if (FAILED(hr)) {
        delete pObj;
    }
    return hr;
}


/*
 * DLL functions
 */

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    CQGAVssProviderFactory *factory;
    try {
        factory = new CQGAVssProviderFactory;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    factory->AddRef();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_nComObjsInUse == 0 ? S_OK : S_FALSE;
}

EXTERN_C
BOOL WINAPI DllMain(HINSTANCE hinstDll, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hinstDll = hinstDll;
        DisableThreadLibraryCalls(hinstDll);
    }
    return TRUE;
}
