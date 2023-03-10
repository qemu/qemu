/*
 * QEMU Guest Agent win32 VSS Requester implementations
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
#include "requester.h"
#include "install.h"
#include <vswriter.h>
#include <vsbackup.h>

/* Max wait time for frozen event (VSS can only hold writes for 10 seconds) */
#define VSS_TIMEOUT_FREEZE_MSEC 60000

/* Call QueryStatus every 10 ms while waiting for frozen event */
#define VSS_TIMEOUT_EVENT_MSEC 10

#define DEFAULT_VSS_BACKUP_TYPE VSS_BT_FULL

#define err_set(e, err, fmt, ...)                                           \
    ((e)->error_setg_win32_wrapper((e)->errp, __FILE__, __LINE__, __func__, \
                                   err, fmt, ## __VA_ARGS__))
/* Bad idea, works only when (e)->errp != NULL: */
#define err_is_set(e) ((e)->errp && *(e)->errp)
/* To lift this restriction, error_propagate(), like we do in QEMU code */

/* Handle to VSSAPI.DLL */
static HMODULE hLib;

/* Functions in VSSAPI.DLL */
typedef HRESULT(STDAPICALLTYPE * t_CreateVssBackupComponents)(
    OUT IVssBackupComponents**);
typedef void(APIENTRY * t_VssFreeSnapshotProperties)(IN VSS_SNAPSHOT_PROP*);
static t_CreateVssBackupComponents pCreateVssBackupComponents;
static t_VssFreeSnapshotProperties pVssFreeSnapshotProperties;

/* Variables used while applications and filesystes are frozen by VSS */
static struct QGAVSSContext {
    IVssBackupComponents *pVssbc;  /* VSS requester interface */
    IVssAsync *pAsyncSnapshot;     /* async info of VSS snapshot operation */
    HANDLE hEventFrozen;           /* notify fs/writer freeze from provider */
    HANDLE hEventThaw;             /* request provider to thaw */
    HANDLE hEventTimeout;          /* notify timeout in provider */
    int cFrozenVols;               /* number of frozen volumes */
} vss_ctx;

STDAPI requester_init(void)
{
    COMInitializer initializer; /* to call CoInitializeSecurity */
    HRESULT hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "failed to CoInitializeSecurity (error %lx)\n", hr);
        return hr;
    }

    hLib = LoadLibraryA("VSSAPI.DLL");
    if (!hLib) {
        fprintf(stderr, "failed to load VSSAPI.DLL\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    pCreateVssBackupComponents = (t_CreateVssBackupComponents)
        GetProcAddress(hLib,
#ifdef _WIN64 /* 64bit environment */
        "?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z"
#else /* 32bit environment */
        "?CreateVssBackupComponents@@YGJPAPAVIVssBackupComponents@@@Z"
#endif
        );
    if (!pCreateVssBackupComponents) {
        fprintf(stderr, "failed to get proc address from VSSAPI.DLL\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    pVssFreeSnapshotProperties = (t_VssFreeSnapshotProperties)
        GetProcAddress(hLib, "VssFreeSnapshotProperties");
    if (!pVssFreeSnapshotProperties) {
        fprintf(stderr, "failed to get proc address from VSSAPI.DLL\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

static void requester_cleanup(void)
{
    if (vss_ctx.hEventFrozen) {
        CloseHandle(vss_ctx.hEventFrozen);
        vss_ctx.hEventFrozen = NULL;
    }
    if (vss_ctx.hEventThaw) {
        CloseHandle(vss_ctx.hEventThaw);
        vss_ctx.hEventThaw = NULL;
    }
    if (vss_ctx.hEventTimeout) {
        CloseHandle(vss_ctx.hEventTimeout);
        vss_ctx.hEventTimeout = NULL;
    }
    if (vss_ctx.pAsyncSnapshot) {
        vss_ctx.pAsyncSnapshot->Release();
        vss_ctx.pAsyncSnapshot = NULL;
    }
    if (vss_ctx.pVssbc) {
        vss_ctx.pVssbc->Release();
        vss_ctx.pVssbc = NULL;
    }
    vss_ctx.cFrozenVols = 0;
}

STDAPI requester_deinit(void)
{
    requester_cleanup();

    pCreateVssBackupComponents = NULL;
    pVssFreeSnapshotProperties = NULL;
    if (hLib) {
        FreeLibrary(hLib);
        hLib = NULL;
    }

    return S_OK;
}

static HRESULT WaitForAsync(IVssAsync *pAsync)
{
    HRESULT ret, hr;

    do {
        hr = pAsync->Wait();
        if (FAILED(hr)) {
            ret = hr;
            break;
        }
        hr = pAsync->QueryStatus(&ret, NULL);
        if (FAILED(hr)) {
            ret = hr;
            break;
        }
    } while (ret == VSS_S_ASYNC_PENDING);

    return ret;
}

static void AddComponents(ErrorSet *errset)
{
    unsigned int cWriters, i;
    VSS_ID id, idInstance, idWriter;
    BSTR bstrWriterName = NULL;
    VSS_USAGE_TYPE usage;
    VSS_SOURCE_TYPE source;
    unsigned int cComponents, c1, c2, j;
    COMPointer<IVssExamineWriterMetadata> pMetadata;
    COMPointer<IVssWMComponent> pComponent;
    PVSSCOMPONENTINFO info;
    HRESULT hr;

    hr = vss_ctx.pVssbc->GetWriterMetadataCount(&cWriters);
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to get writer metadata count");
        goto out;
    }

    for (i = 0; i < cWriters; i++) {
        hr = vss_ctx.pVssbc->GetWriterMetadata(i, &id, pMetadata.replace());
        if (FAILED(hr)) {
            err_set(errset, hr, "failed to get writer metadata of %d/%d",
                             i, cWriters);
            goto out;
        }

        hr = pMetadata->GetIdentity(&idInstance, &idWriter,
                                    &bstrWriterName, &usage, &source);
        if (FAILED(hr)) {
            err_set(errset, hr, "failed to get identity of writer %d/%d",
                             i, cWriters);
            goto out;
        }

        hr = pMetadata->GetFileCounts(&c1, &c2, &cComponents);
        if (FAILED(hr)) {
            err_set(errset, hr, "failed to get file counts of %S",
                             bstrWriterName);
            goto out;
        }

        for (j = 0; j < cComponents; j++) {
            hr = pMetadata->GetComponent(j, pComponent.replace());
            if (FAILED(hr)) {
                err_set(errset, hr,
                                 "failed to get component %d/%d of %S",
                                 j, cComponents, bstrWriterName);
                goto out;
            }

            hr = pComponent->GetComponentInfo(&info);
            if (FAILED(hr)) {
                err_set(errset, hr,
                                 "failed to get component info %d/%d of %S",
                                 j, cComponents, bstrWriterName);
                goto out;
            }

            if (info->bSelectable) {
                hr = vss_ctx.pVssbc->AddComponent(idInstance, idWriter,
                                                  info->type,
                                                  info->bstrLogicalPath,
                                                  info->bstrComponentName);
                if (FAILED(hr)) {
                    err_set(errset, hr, "failed to add component %S(%S)",
                                     info->bstrComponentName, bstrWriterName);
                    goto out;
                }
            }
            SysFreeString(bstrWriterName);
            bstrWriterName = NULL;
            pComponent->FreeComponentInfo(info);
            info = NULL;
        }
    }
out:
    if (bstrWriterName) {
        SysFreeString(bstrWriterName);
    }
    if (pComponent && info) {
        pComponent->FreeComponentInfo(info);
    }
}

DWORD get_reg_dword_value(HKEY baseKey, LPCSTR subKey, LPCSTR valueName,
                          DWORD defaultData)
{
    DWORD regGetValueError;
    DWORD dwordData;
    DWORD dataSize = sizeof(DWORD);

    regGetValueError = RegGetValue(baseKey, subKey, valueName, RRF_RT_DWORD,
                                   NULL, &dwordData, &dataSize);
    if (regGetValueError  != ERROR_SUCCESS) {
        return defaultData;
    }
    return dwordData;
}

bool is_valid_vss_backup_type(VSS_BACKUP_TYPE vssBT)
{
    return (vssBT > VSS_BT_UNDEFINED && vssBT < VSS_BT_OTHER);
}

VSS_BACKUP_TYPE get_vss_backup_type(
    VSS_BACKUP_TYPE defaultVssBT = DEFAULT_VSS_BACKUP_TYPE)
{
    VSS_BACKUP_TYPE vssBackupType;

    vssBackupType = static_cast<VSS_BACKUP_TYPE>(
                            get_reg_dword_value(HKEY_LOCAL_MACHINE,
                                                QGA_PROVIDER_REGISTRY_ADDRESS,
                                                "VssOption",
                                                defaultVssBT));
    if (!is_valid_vss_backup_type(vssBackupType)) {
        return defaultVssBT;
    }
    return vssBackupType;
}

void requester_freeze(int *num_vols, void *mountpoints, ErrorSet *errset)
{
    COMPointer<IVssAsync> pAsync;
    HANDLE volume;
    HRESULT hr;
    LONG ctx;
    GUID guidSnapshotSet = GUID_NULL;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES sa;
    WCHAR short_volume_name[64], *display_name = short_volume_name;
    DWORD wait_status;
    int num_fixed_drives = 0, i;
    int num_mount_points = 0;
    VSS_BACKUP_TYPE vss_bt = get_vss_backup_type();

    if (vss_ctx.pVssbc) { /* already frozen */
        *num_vols = 0;
        return;
    }

    CoInitialize(NULL);

    /* Allow unrestricted access to events */
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    vss_ctx.hEventFrozen = CreateEvent(&sa, TRUE, FALSE, EVENT_NAME_FROZEN);
    if (!vss_ctx.hEventFrozen) {
        err_set(errset, GetLastError(), "failed to create event %s",
                EVENT_NAME_FROZEN);
        goto out;
    }
    vss_ctx.hEventThaw = CreateEvent(&sa, TRUE, FALSE, EVENT_NAME_THAW);
    if (!vss_ctx.hEventThaw) {
        err_set(errset, GetLastError(), "failed to create event %s",
                EVENT_NAME_THAW);
        goto out;
    }
    vss_ctx.hEventTimeout = CreateEvent(&sa, TRUE, FALSE, EVENT_NAME_TIMEOUT);
    if (!vss_ctx.hEventTimeout) {
        err_set(errset, GetLastError(), "failed to create event %s",
                EVENT_NAME_TIMEOUT);
        goto out;
    }

    assert(pCreateVssBackupComponents != NULL);
    hr = pCreateVssBackupComponents(&vss_ctx.pVssbc);
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to create VSS backup components");
        goto out;
    }

    hr = vss_ctx.pVssbc->InitializeForBackup();
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to initialize for backup");
        goto out;
    }

    hr = vss_ctx.pVssbc->SetBackupState(true, true, vss_bt, false);
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to set backup state");
        goto out;
    }

    /*
     * Currently writable snapshots are not supported.
     * To prevent the final commit (which requires to write to snapshots),
     * ATTR_NO_AUTORECOVERY and ATTR_TRANSPORTABLE are specified here.
     */
    ctx = VSS_CTX_APP_ROLLBACK | VSS_VOLSNAP_ATTR_TRANSPORTABLE |
        VSS_VOLSNAP_ATTR_NO_AUTORECOVERY | VSS_VOLSNAP_ATTR_TXF_RECOVERY;
    hr = vss_ctx.pVssbc->SetContext(ctx);
    if (hr == (HRESULT)VSS_E_UNSUPPORTED_CONTEXT) {
        /* Non-server version of Windows doesn't support ATTR_TRANSPORTABLE */
        ctx &= ~VSS_VOLSNAP_ATTR_TRANSPORTABLE;
        hr = vss_ctx.pVssbc->SetContext(ctx);
    }
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to set backup context");
        goto out;
    }

    hr = vss_ctx.pVssbc->GatherWriterMetadata(pAsync.replace());
    if (SUCCEEDED(hr)) {
        hr = WaitForAsync(pAsync);
    }
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to gather writer metadata");
        goto out;
    }

    AddComponents(errset);
    if (err_is_set(errset)) {
        goto out;
    }

    hr = vss_ctx.pVssbc->StartSnapshotSet(&guidSnapshotSet);
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to start snapshot set");
        goto out;
    }

    if (mountpoints) {
        PWCHAR volume_name_wchar;
        for (volList *list = (volList *)mountpoints; list; list = list->next) {
            size_t len = strlen(list->value) + 1;
            size_t converted = 0;
            VSS_ID pid;

            volume_name_wchar = new wchar_t[len];
            mbstowcs_s(&converted, volume_name_wchar, len,
                       list->value, _TRUNCATE);

            hr = vss_ctx.pVssbc->AddToSnapshotSet(volume_name_wchar,
                                                  g_gProviderId, &pid);
            if (FAILED(hr)) {
                err_set(errset, hr, "failed to add %S to snapshot set",
                        volume_name_wchar);
                delete[] volume_name_wchar;
                goto out;
            }
            num_mount_points++;

            delete[] volume_name_wchar;
        }

        if (num_mount_points == 0) {
            /* If there is no valid mount points, just exit. */
            goto out;
        }
    }

    if (!mountpoints) {
        volume = FindFirstVolumeW(short_volume_name, sizeof(short_volume_name));
        if (volume == INVALID_HANDLE_VALUE) {
            err_set(errset, hr, "failed to find first volume");
            goto out;
        }

        for (;;) {
            if (GetDriveTypeW(short_volume_name) == DRIVE_FIXED) {
                VSS_ID pid;
                hr = vss_ctx.pVssbc->AddToSnapshotSet(short_volume_name,
                                                      g_gProviderId, &pid);
                if (FAILED(hr)) {
                    WCHAR volume_path_name[PATH_MAX];
                    if (GetVolumePathNamesForVolumeNameW(
                            short_volume_name, volume_path_name,
                            sizeof(volume_path_name), NULL) &&
                            *volume_path_name) {
                        display_name = volume_path_name;
                    }
                    err_set(errset, hr, "failed to add %S to snapshot set",
                            display_name);
                    FindVolumeClose(volume);
                    goto out;
                }
                num_fixed_drives++;
            }
            if (!FindNextVolumeW(volume, short_volume_name,
                                 sizeof(short_volume_name))) {
                FindVolumeClose(volume);
                break;
            }
        }

        if (num_fixed_drives == 0) {
            goto out; /* If there is no fixed drive, just exit. */
        }
    }

    hr = vss_ctx.pVssbc->PrepareForBackup(pAsync.replace());
    if (SUCCEEDED(hr)) {
        hr = WaitForAsync(pAsync);
    }
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to prepare for backup");
        goto out;
    }

    hr = vss_ctx.pVssbc->GatherWriterStatus(pAsync.replace());
    if (SUCCEEDED(hr)) {
        hr = WaitForAsync(pAsync);
    }
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to gather writer status");
        goto out;
    }

    /*
     * Start VSS quiescing operations.
     * CQGAVssProvider::CommitSnapshots will kick vss_ctx.hEventFrozen
     * after the applications and filesystems are frozen.
     */
    hr = vss_ctx.pVssbc->DoSnapshotSet(&vss_ctx.pAsyncSnapshot);
    if (FAILED(hr)) {
        err_set(errset, hr, "failed to do snapshot set");
        goto out;
    }

    /* Need to call QueryStatus several times to make VSS provider progress */
    for (i = 0; i < VSS_TIMEOUT_FREEZE_MSEC/VSS_TIMEOUT_EVENT_MSEC; i++) {
        HRESULT hr2 = vss_ctx.pAsyncSnapshot->QueryStatus(&hr, NULL);
        if (FAILED(hr2)) {
            err_set(errset, hr, "failed to do snapshot set");
            goto out;
        }
        if (hr != VSS_S_ASYNC_PENDING) {
            err_set(errset, E_FAIL,
                    "DoSnapshotSet exited without Frozen event");
            goto out;
        }
        wait_status = WaitForSingleObject(vss_ctx.hEventFrozen,
                                          VSS_TIMEOUT_EVENT_MSEC);
        if (wait_status != WAIT_TIMEOUT) {
            break;
        }
    }

    if (wait_status == WAIT_TIMEOUT) {
        err_set(errset, E_FAIL,
                "timeout when try to receive Frozen event from VSS provider");
        /* If we are here, VSS had timeout.
         * Don't call AbortBackup, just return directly.
         */
        goto out1;
    }

    if (wait_status != WAIT_OBJECT_0) {
        err_set(errset, E_FAIL,
                "couldn't receive Frozen event from VSS provider");
        goto out;
    }

    if (mountpoints) {
        *num_vols = vss_ctx.cFrozenVols = num_mount_points;
    } else {
        *num_vols = vss_ctx.cFrozenVols = num_fixed_drives;
    }

    return;

out:
    if (vss_ctx.pVssbc) {
        vss_ctx.pVssbc->AbortBackup();
    }

out1:
    requester_cleanup();
    CoUninitialize();
}


void requester_thaw(int *num_vols, void *mountpints, ErrorSet *errset)
{
    COMPointer<IVssAsync> pAsync;

    if (!vss_ctx.hEventThaw) {
        /*
         * In this case, DoSnapshotSet is aborted or not started,
         * and no volumes must be frozen. We return without an error.
         */
        *num_vols = 0;
        return;
    }

    /* Tell the provider that the snapshot is finished. */
    SetEvent(vss_ctx.hEventThaw);

    assert(vss_ctx.pVssbc);
    assert(vss_ctx.pAsyncSnapshot);

    HRESULT hr = WaitForAsync(vss_ctx.pAsyncSnapshot);
    switch (hr) {
    case VSS_S_ASYNC_FINISHED:
        hr = vss_ctx.pVssbc->BackupComplete(pAsync.replace());
        if (SUCCEEDED(hr)) {
            hr = WaitForAsync(pAsync);
        }
        if (FAILED(hr)) {
            err_set(errset, hr, "failed to complete backup");
        }
        break;

    case (HRESULT)VSS_E_OBJECT_NOT_FOUND:
        /*
         * On Windows earlier than 2008 SP2 which does not support
         * VSS_VOLSNAP_ATTR_NO_AUTORECOVERY context, the final commit is not
         * skipped and VSS is aborted by VSS_E_OBJECT_NOT_FOUND. However, as
         * the system had been frozen until fsfreeze-thaw command was issued,
         * we ignore this error.
         */
        vss_ctx.pVssbc->AbortBackup();
        break;

    case VSS_E_UNEXPECTED_PROVIDER_ERROR:
        if (WaitForSingleObject(vss_ctx.hEventTimeout, 0) != WAIT_OBJECT_0) {
            err_set(errset, hr, "unexpected error in VSS provider");
            break;
        }
        /* fall through if hEventTimeout is signaled */

    case (HRESULT)VSS_E_HOLD_WRITES_TIMEOUT:
        err_set(errset, hr, "couldn't hold writes: "
                "fsfreeze is limited up to 10 seconds");
        break;

    default:
        err_set(errset, hr, "failed to do snapshot set");
    }

    if (err_is_set(errset)) {
        vss_ctx.pVssbc->AbortBackup();
    }
    *num_vols = vss_ctx.cFrozenVols;
    requester_cleanup();

    CoUninitialize();
    StopService();
}
