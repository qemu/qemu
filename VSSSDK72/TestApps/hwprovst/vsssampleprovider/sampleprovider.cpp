/*--

Copyright (C) Microsoft Corporation, 2003

Module Name:

    SampleProvider.cpp

Abstract:

    Implementation of the CSampleProvider class, a sample VSS HW provider
    that makes use of a virtual disk driver to create snapshots.

Notes:

Revision History:

--*/

#include "stdafx.h"
#include "SampleProvider.h"

// CSampleProvider methods

CSampleProvider::CSampleProvider(
    )
    : m_state( VSS_SS_UNKNOWN )
{
    TRACE_FUNCTION();

    memset(&m_setId, 0, sizeof m_setId);
    InitializeCriticalSection( &m_cs );
}

CSampleProvider::~CSampleProvider(
    )
{
    TRACE_FUNCTION();

    //
    // Cleanup any in-progress LUNs by unloading.  The OnUnload() call
    // will mostly be redundant since VSS should have called it
    // before releasing the interface, but this is harmless and
    // provides extra safety.
    //
    OnUnload(TRUE); 

    DeleteCriticalSection( &m_cs );
}

// Helpers

void
CSampleProvider::FreeLunInfo(
    VDS_LUN_INFORMATION& lun
    )
{
    SAFE_COFREE( lun.m_szVendorId );
    SAFE_COFREE( lun.m_szProductId );
    SAFE_COFREE( lun.m_szProductRevision );
    SAFE_COFREE( lun.m_szSerialNumber );

    VDS_STORAGE_DEVICE_ID_DESCRIPTOR& desc = lun.m_deviceIdDescriptor;
    for (ULONG i = 0; i < desc.m_cIdentifiers; ++i) {
        SAFE_COFREE( desc.m_rgIdentifiers[i].m_rgbIdentifier );
    }
    SAFE_COFREE( desc.m_rgIdentifiers );

    for (ULONG i = 0; i < lun.m_cInterconnects; ++i) {
        VDS_INTERCONNECT& inter = lun.m_rgInterconnects[i];
        SAFE_COFREE( inter.m_pbPort );
        SAFE_COFREE( inter.m_pbAddress );
    }
    SAFE_COFREE( lun.m_rgInterconnects );
}

void
CSampleProvider::CopyBasicLunInfo(
    VDS_LUN_INFORMATION& lunDst,
    VDS_LUN_INFORMATION& lunSrc
    )
{
    lunDst.m_version = lunSrc.m_version;
    lunDst.m_DeviceType = lunSrc.m_DeviceType;
    lunDst.m_DeviceTypeModifier = lunSrc.m_DeviceTypeModifier;
    lunDst.m_bCommandQueueing = lunSrc.m_bCommandQueueing;
    lunDst.m_BusType = lunSrc.m_BusType;

    //
    // These NewString() calls may throw HRESULT exceptions, the
    // caller should be prepared to deal with them
    //
    lunDst.m_szVendorId = NewString( lunSrc.m_szVendorId );
    lunDst.m_szProductId = NewString( lunSrc.m_szProductId );
    lunDst.m_szProductRevision = NewString( lunSrc.m_szProductRevision );
    lunDst.m_szSerialNumber = NewString( lunSrc.m_szSerialNumber );

    lunDst.m_diskSignature = lunSrc.m_diskSignature;
}

void
CSampleProvider::DisplayLunInfo(
    VDS_LUN_INFORMATION& lun
    )
{
    TRACE_FUNCTION();

    TraceMsg(L"Initial: m_deviceIdDescriptor.m_cIdentifiers=%d, m_deviceIdDescriptor.m_rgIdentifiers=0x%08x\n",
             lun.m_deviceIdDescriptor.m_cIdentifiers,
             lun.m_deviceIdDescriptor.m_rgIdentifiers);
    TraceMsg(L"Initial: m_cInterconnects=%d, m_rgInterconnects=0x%08x\n",
             lun.m_cInterconnects,
             lun.m_rgInterconnects);
}

//
// This function makes a best effort to delete any outstanding snapshots, but
// will not indicate errors and will never throw an exception.
//
void
CSampleProvider::DeleteAbortedSnapshots(
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;
    SnapshotInfoVector::iterator i;
    
    AutoLock lock(m_cs);

    for (i = m_vSnapshotInfo.begin(); i != m_vSnapshotInfo.end(); ++i) {
        try {
            //
            // We ignore errors here, since the drive may not yet have even
            // been created.
            //
            hr = m_vbus.RemoveDrive(i->snapLunId, false);

            std::wstring fileName = SnapshotImageFile(i->snapLunId);
            int i = 0;
            while (DeleteFile(fileName.c_str()) == FALSE &&
                   GetLastError() == ERROR_SHARING_VIOLATION && ++ i < 5)
            {
                //
                // Sleep 2 seconds to wait for the virtual storage driver to
                // release the snapshot image file.
                // Retry 5 times in total. Ignore if the file cannot be
                // deleted after 5 times, or if the error is not due
                // to ERROR_SHARING_VIOLATION
                //
                Sleep(2000);
            }
        } catch (std::bad_alloc) {
            // Ignore out of memory errors, just do best effort to delete
            // snapshots.
        }
    }

    m_vSnapshotInfo.clear();
}

BOOL
CSampleProvider::FindSnapId(
    GUID origLunId,
    GUID& snapLunId
    )
{
    SnapshotInfoVector::iterator i;
    for (i = m_vSnapshotInfo.begin(); i != m_vSnapshotInfo.end(); ++i) {
        if (IsEqualGUID(i->origLunId, origLunId) == TRUE) {
            snapLunId = i->snapLunId;
            return TRUE;
        }
    }

    return FALSE;
}

BOOL
CSampleProvider::FindOrigId(
    GUID snapLunId,
    GUID& origLunId
    )
{
    SnapshotInfoVector::iterator i;
    for (i = m_vSnapshotInfo.begin(); i != m_vSnapshotInfo.end(); ++i) {
        if (IsEqualGUID(i->snapLunId, snapLunId) == TRUE) {
            origLunId = i->origLunId;
            return TRUE;
        }
    }

    return FALSE;
}

std::wstring
CSampleProvider::SnapshotImageFile(
    GUID snapLunId
    )
{
    HRESULT hr = S_OK;
    std::wstring sysDrive;

    hr = GetEnvVar(std::wstring(L"SystemDrive"), sysDrive);
    if (SUCCEEDED( hr )) {
        return sysDrive + std::wstring(L"\\") + GuidToWString(snapLunId) + L".image";
    } else {
        return std::wstring(L"C:\\") + GuidToWString(snapLunId) + L".image";
    }
}

//
// IVssHardwareSnapshotProvider methods
//

STDMETHODIMP
CSampleProvider::AreLunsSupported(
    IN LONG lLunCount,
    IN LONG lContext,
    IN VSS_PWSZ* rgwszDevices,
    IN OUT VDS_LUN_INFORMATION* rgLunInformation,
    OUT BOOL* pbIsSupported
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );
    
    try {

        *pbIsSupported = FALSE;

        for (int i = 0; i < lLunCount; i++) {
            GUID gId;
            hr = AnsiToGuid(rgLunInformation[i].m_szSerialNumber, gId);
            switch (hr) {
            case S_OK:
                break;

            case E_OUTOFMEMORY:
                //
                // Allow out of memory errors to float up
                //
                throw hr;
                    
            default:
                //
                // An invalid GUID isn't a failure, it just means that
                // the LUN can't possibly be one of ours.  In this
                // case we should still return S_OK with
                // *pbIsSupported = FALSE.
                //
                hr = S_OK;
                goto done;
            }

            //
            // Query the virtual storage driver for info on this drive,
            // if the driver returns an error then the drive isn't one
            // of ours.  In this case we should still return S_OK with
            // *pbIsSupported = FALSE;
            //
            VirtualStorage::VirtualBus::STORAGE_INFORMATION storageInfo;
            HRESULT hrv = m_vbus.QueryStorageInformation(gId, storageInfo);
            if (hrv != S_OK) {
                goto done;
            }

            //
            // Due to a bug in VSS, the snapshot will fail if the bus
            // type is VDSBusTypeUnknown (VirtualStorage driver
            // returns this value).  The workaround is to fudge the
            // bus type here.
            // 
            rgLunInformation[i].m_BusType = VDSBusTypeScsi;
        }

        *pbIsSupported = TRUE;

    } catch(HRESULT hre) {
        hr = hre;
    } catch(std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

 done:
    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::GetTargetLuns(
    IN LONG lLunCount,
    IN VSS_PWSZ* rgwszDevices,
    IN VDS_LUN_INFORMATION* rgSourceLuns,
    IN OUT VDS_LUN_INFORMATION* rgDestinationLuns
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );
    
    try {
        for (LONG i = 0; i < lLunCount; i++) {
            VDS_LUN_INFORMATION& lunSource = rgSourceLuns[i];
            VDS_LUN_INFORMATION& lunTarget = rgDestinationLuns[i];

            FreeLunInfo(lunTarget);
            CopyBasicLunInfo(lunTarget, lunSource);
            memset(&lunTarget.m_diskSignature, 0, sizeof lunTarget.m_diskSignature);

            GUID origId, snapId;
            hr = AnsiToGuid(lunSource.m_szSerialNumber, origId);
            switch (hr) {
            case S_OK:
                break;

            case E_OUTOFMEMORY:
                //
                // Allow out of memory errors to float up
                //
                throw hr;

            default:
                //
                // Any other error indicates we were passed a bad source LUN,
                // so log it and return VSS_E_PROVIDER_VETO
                //
                LogEvent(L"GetTargetLuns called with invalid source LUN ('%S')",
                                 lunSource.m_szSerialNumber);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            //
            // Find the snapshot GUID associated with this LUN
            //
            BOOL b = FindSnapId(origId, snapId);
            if (b == FALSE) {
                LogEvent(L"GetTargetLuns called with unknown LUN ('%S')",
                                 lunSource.m_szSerialNumber);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            SAFE_COFREE(lunTarget.m_szSerialNumber);
            lunTarget.m_szSerialNumber = GuidToAnsi(snapId);

            //
            // Due to a bug in VSS, the snapshot will fail if the bus
            // type is VDSBusTypeUnknown (VirtualStorage driver
            // returns this value).  The workaround is to fudge the
            // bus type here.
            // 
            lunTarget.m_BusType = VDSBusTypeScsi;

        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::LocateLuns(
    IN LONG lLunCount,
    IN VDS_LUN_INFORMATION* rgSourceLuns
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        for (LONG i = 0; i < lLunCount; i++) {
            VDS_LUN_INFORMATION& lunSource = rgSourceLuns[i];

            GUID snapId;
            hr = AnsiToGuid(lunSource.m_szSerialNumber, snapId);
            switch (hr) {
            case S_OK:
                break;

            case E_OUTOFMEMORY:
                //
                // Allow out of memory errors to float up
                //
                throw hr;

            default:
                //
                // Any other error indicates we were passed a bad source LUN,
                // so log it and return VSS_E_PROVIDER_VETO
                //
                LogEvent(L"LocateLuns called with invalid source LUN m_szSerialNumber ('%S')",
                         lunSource.m_szSerialNumber);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            //
            // Calculate size of image file
            // If the snapshot ID is invalid, CreateFile will fail and
            // so log it and return VSS_E_PROVIDER_VETO
            //
            std::wstring fileName = SnapshotImageFile(snapId);
            HANDLE hFile = CreateFile( fileName.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL );
            if (hFile == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                LogEvent( L"Error opening image file '%S' (%d)",
                          fileName.c_str(),
                          err );
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            LARGE_INTEGER fileSize;
            BOOL br = GetFileSizeEx( hFile, &fileSize );
            if (br == FALSE) {
                DWORD err = GetLastError();
                LogEvent( L"Error getting size of image file '%S' (%d)",
                          fileName.c_str(),
                          err );
                CloseHandle(hFile);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            CloseHandle(hFile);

            //
            // Create new virtual drive using image file
            //
            size_t sizeStruct = sizeof(NEW_VIRTUAL_DRIVE_DESCRIPTION);
            std::wstring strImagePath(L"\\??\\" + fileName);
            sizeStruct += (strImagePath.size() * sizeof(WCHAR));
            sizeStruct += 64; // Add a 64 byte storage ID blob (all zeros)

            std::vector<BYTE> vecData(sizeStruct,0);
            NEW_VIRTUAL_DRIVE_DESCRIPTION* pInfoDrive = reinterpret_cast<NEW_VIRTUAL_DRIVE_DESCRIPTION*>(&vecData[0]);

            pInfoDrive->Length = static_cast<USHORT>(sizeStruct);
            pInfoDrive->BlockSize = 512;
            pInfoDrive->NumberOfBlocks = static_cast<ULONG>( fileSize.QuadPart / pInfoDrive->BlockSize );
            pInfoDrive->Flags = 0;
            pInfoDrive->DeviceType = VIRTUAL_FIXED_DISK;
            pInfoDrive->DriveID = snapId;
            pInfoDrive->FileNameOffset = 0;
            pInfoDrive->FileNameLength = static_cast<USHORT>(strImagePath.size() * sizeof(WCHAR));
            wcsncpy((WCHAR*) pInfoDrive->Buffer, strImagePath.c_str(), strImagePath.size());
            pInfoDrive->StorageDeviceIdDescOffset = pInfoDrive->FileNameLength;
            pInfoDrive->StorageDeviceIdDescLength = 64;

            VIRTUAL_DRIVE_INFORMATION driveInfo;
            hr = m_vbus.CreateDriveEx( pInfoDrive, driveInfo );

            switch (hr) {
            case S_OK:
                break;

            default:
                LogEvent(L"CreateDriveEx for '%S' failed with 0x%08x",
                         lunSource.m_szSerialNumber,
                         hr);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::FillInLunInfo(
    IN VSS_PWSZ wszDeviceName,
    IN OUT VDS_LUN_INFORMATION* pLunInformation,
    OUT BOOL* pbIsSupported
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        //
        // We see if the new LUN is one of ours, and modify the
        // m_szSerialNumber field to match the GUID format that we set
        // in GetTargetLuns(), since the case and format are not
        // necessarily the same.  Luns that are not ours are ignored
        // and pbIsSupported is set to FALSE.
        //

        *pbIsSupported = FALSE;

        GUID snapId;
        hr = AnsiToGuid(pLunInformation->m_szSerialNumber, snapId);
        switch (hr) {
        case S_OK:
            break;

        case E_OUTOFMEMORY:
            throw hr;

        default:
            //
            // An invalid GUID indicates that the LUN can't be ours,
            // just skip it.
            //
            hr = S_OK;
            goto done;
        }

        //
        // Query the virtual storage driver for info on this drive,
        // if the driver returns an error then the drive isn't one
        // of ours.  In this case we just ignore it.
        //
        VirtualStorage::VirtualBus::STORAGE_INFORMATION storageInfo;
        HRESULT hrv = m_vbus.QueryStorageInformation(snapId, storageInfo);
        if (hrv != S_OK) {
            goto done;
        }

        SAFE_COFREE(pLunInformation->m_szSerialNumber);
        pLunInformation->m_szSerialNumber = GuidToAnsi(snapId);
        *pbIsSupported = TRUE;

        //
        // Due to a bug in VSS, the snapshot will fail if the bus
        // type is VDSBusTypeUnknown (VirtualStorage driver
        // returns this value).  The workaround is to fudge the
        // bus type here.
        // 
        pLunInformation->m_BusType = VDSBusTypeScsi;

    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

 done:
    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::OnLunEmpty(
    IN VSS_PWSZ wszDevice,
    IN VDS_LUN_INFORMATION* pInfo
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        GUID snapId;
        hr = AnsiToGuid(pInfo->m_szSerialNumber, snapId);
        switch (hr) {
        case S_OK:
            break;

        case E_OUTOFMEMORY:
            throw hr;

        default:
            //
            // Any other error indicates we were passed a bad source LUN,
            // so log it and return VSS_E_PROVIDER_VETO
            //
            LogEvent(L"OnLunEmpty called with invalid LUN ('%S')",
                             pInfo->m_szSerialNumber);
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }

        hr = m_vbus.RemoveDrive(snapId, false);
        if (FAILED(hr)) {
            //
            // If we can't delete the LUN, log and return VSS_E_PROVIDER_VETO
            //
            LogEvent(L"RemoveDrive for '%S' failed with 0x%08x",
                             pInfo->m_szSerialNumber,
                             hr);
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }

        std::wstring fileName = SnapshotImageFile(snapId);

        int i = 0;
        while (DeleteFile(fileName.c_str()) == FALSE) {
            DWORD err = GetLastError();
            if(err == ERROR_SHARING_VIOLATION && ++ i < 5)
            {
                //
                // Sleep 2 seconds to wait for the virtual storage driver to
                // release the snapshot image file.
                // Retry 5 times in total.
                //
                Sleep(2000);
                continue;
            }

            //
            // If we can't delete the image file, log and return error
            //
            LogEvent(L"DeleteFile for '%S' failed (%d)",
                             pInfo->m_szSerialNumber,
                             err);
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::BeginPrepareSnapshot(
    IN VSS_ID SnapshotSetId,
    IN VSS_ID SnapshotId,
    IN LONG lContext,
    IN LONG lLunCount,
    IN OUT VSS_PWSZ* rgwszDevices,
    IN VDS_LUN_INFORMATION* rgLunInformation
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_PREPARING:
            //
            // If we get a new snapshot set id, then we are starting a
            // new snapshot and we should delete any uncompleted
            // snapshots.  Otherwise continue to add LUNs to the set.
            //
            if (!IsEqualGUID(SnapshotSetId, m_setId)) {
                DeleteAbortedSnapshots();
            }
            break;

        case VSS_SS_UNKNOWN:
        case VSS_SS_CREATED:
        case VSS_SS_ABORTED:
            //
            // If we are in the initial state, or completed/aborted
            // the previous snapshot, initialize the list of LUNs
            // participating in this snapshot
            //
            m_vSnapshotInfo.clear();
            break;

        default:
            //
            // If we were in any other state we should abort the
            // current snapshot and delete any in-progess snapshots
            //
            DeleteAbortedSnapshots();
            break;
        }

        for (LONG i = 0; i < lLunCount; i++) {
            GUID origId;
            GUID snapId;

            hr = AnsiToGuid(rgLunInformation[i].m_szSerialNumber, origId);
            switch (hr) {
            case S_OK:
                break;

            case E_OUTOFMEMORY:
                throw hr;

            default:
                //
                // Any other error indicates we were passed a bad source LUN,
                // so log it and return VSS_E_PROVIDER_VETO
                //
                LogEvent(L"BeginPrepareSnapshot called with invalid LUN ('%S')",
                                 rgLunInformation[i].m_szSerialNumber);
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            }

            //
            // If we already have this LUN included in this snapshot set, skip it
            //
            if (FindSnapId(origId, snapId)) {
                continue;
            }

            //
            // Create a unique GUID to represent the snapshot drive.
            // A real provider might ask the array to prepare to
            // create the LUN but not expose or commit the snapshot.
            //
            CoCreateGuid( &snapId );

            //
            // Associate the original LUN with the snapshot LUN.
            //
            SnapshotInfo infoSnap;
            infoSnap.origLunId = origId;
            infoSnap.snapLunId = snapId;
            m_vSnapshotInfo.push_back(infoSnap);

            m_state = VSS_SS_PREPARING;
            m_setId = SnapshotSetId;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        DeleteAbortedSnapshots();
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

//
// IVssProviderCreateSnapshotSet methods
//

STDMETHODIMP
CSampleProvider::EndPrepareSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_PREPARING:
            if (!IsEqualGUID(SnapshotSetId, m_setId)) {
                LogEvent(L"Unexpected snapshot set ID during EndPrepareSnapshots");
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            } else {
                m_state = VSS_SS_PREPARED;
            }
            break;

        default:
            LogEvent(L"EndPrepareSnapshots called out of order");
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc()) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        DeleteAbortedSnapshots();
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::PreCommitSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_PREPARED:
            if (!IsEqualGUID(SnapshotSetId, m_setId)) {
                LogEvent(L"Unexpected snapshot set ID during PreCommitSnapshots");
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            } else {
                m_state = VSS_SS_PRECOMMITTED;
            }
            break;

        default:
            LogEvent(L"PreCommitSnapshots called out of order");
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        DeleteAbortedSnapshots();
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::CommitSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_PRECOMMITTED:
            if (!IsEqualGUID(SnapshotSetId, m_setId)) {
                LogEvent(L"Unexpected snapshot set ID during CommitSnapshots");
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            } else {
                //
                // Actually perform the snapshot for each LUN in the set
                //
                SnapshotInfoVector::iterator i;
                for (i = m_vSnapshotInfo.begin(); i != m_vSnapshotInfo.end(); ++i) {
                    //
                    // Find the image file associated with the
                    // original id and "commit" the snapshot by
                    // copying to the snapshot image.  This is time
                    // critical and should take 10s or less.  For a
                    // real implementation it is preferable to start
                    // the commit for all LUNs in the set at the same
                    // time and then wait for them all to finish
                    // instead of processing each one serially.
                    //
                    // This implementation is also flawed in that the
                    // time to commit could easily exceed the 10s
                    // window if the LUNs are greater than a few MB.
                    //

                    std::wstring origImage;
                    hr = m_vbus.QueryMountedImage(i->origLunId, origImage);
                    if (FAILED(hr)) {
                        LogEvent(L"Unable to find image for LUN during CommitSnapshots");
                        throw (HRESULT) VSS_E_PROVIDER_VETO;
                    }
                    
                    std::wstring snapImage = SnapshotImageFile(i->snapLunId);
                    BOOL br = CopyFile(origImage.c_str(), snapImage.c_str(), FALSE);
                    if (br == FALSE) {
                        DWORD err = GetLastError();
                        LogEvent( L"Error copying image file from '%S' to '%S' (%d)",
                                          origImage.c_str(),
                                          snapImage.c_str(),
                                          err );
                        throw (HRESULT) VSS_E_PROVIDER_VETO;
                    }
                }
                m_state = VSS_SS_COMMITTED;
            }
            break;
        default:
            LogEvent(L"CommitSnapshots called out of order");
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        DeleteAbortedSnapshots();
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::PostCommitSnapshots(
    IN VSS_ID SnapshotSetId,
    IN LONG lSnapshotsCount
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_COMMITTED:
            if (!IsEqualGUID(SnapshotSetId, m_setId)) {
                LogEvent(L"Unexpected snapshot set ID during PostCommitSnapshots");
                throw (HRESULT) VSS_E_PROVIDER_VETO;
            } else {
                m_state = VSS_SS_CREATED;
            }
            break;

        default:
            LogEvent(L"PostCommitSnapshots called out of order");
            throw (HRESULT) VSS_E_PROVIDER_VETO;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        DeleteAbortedSnapshots();
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

//
// These two methods are stubs for Windows Server 2003, and should merely return S_OK
//

STDMETHODIMP
CSampleProvider::PreFinalCommitSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;
    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::PostFinalCommitSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;
    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

STDMETHODIMP
CSampleProvider::AbortSnapshots(
    IN VSS_ID SnapshotSetId
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_CREATED:
            //
            // Aborts are ignored after create
            //
            m_state = VSS_SS_CREATED;
            break;

        default:
            DeleteAbortedSnapshots();
            m_state = VSS_SS_ABORTED;
            break;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    if (hr != S_OK) {
        m_state = VSS_SS_ABORTED;
    }

    TraceMsg(L"returning 0x%08x\n", hr);
    return hr;
}

//
// IVssProviderNotifications methods
//

STDMETHODIMP
CSampleProvider::OnLoad(
    IN IUnknown *
    )
{
    TRACE_FUNCTION();

    return S_OK;
}

STDMETHODIMP
CSampleProvider::OnUnload(
    IN BOOL /* bForceUnload */
    )
{
    TRACE_FUNCTION();

    HRESULT hr = S_OK;

    AutoLock lock( m_cs );

    try {
        switch (m_state) {
        case VSS_SS_UNKNOWN:
        case VSS_SS_ABORTED:
        case VSS_SS_CREATED:
            break;
        
        default:
            //
            // Treat unloading during snapshot creation as an abort
            //
            DeleteAbortedSnapshots();
            break;
        }
    } catch (HRESULT hre) {
        hr = hre;
    } catch (std::bad_alloc) {
        hr = E_OUTOFMEMORY;
    }

    m_state = VSS_SS_UNKNOWN;

    TraceMsg(L"returning 0x%08x\n", hr);

    return hr;
}
