
/*--

Copyright (C) Microsoft Corporation, 2003

Module Name:

    SampleProvider.h

Abstract:

    SampleProvider.h : Declaration of the CSampleProvider class, a sample VSS HW provider
    that makes use of a virtual disk driver to create snapshots.

Notes:

Revision History:

--*/

#pragma once
#include "resource.h"       // main symbols

#include "VssSampleProvider.h"

// CSampleProvider

class ATL_NO_VTABLE CSampleProvider : 
    public CComObjectRootEx<CComSingleThreadModel>,
    public CComCoClass<CSampleProvider, &CLSID_SampleProvider>,
    public IVssHardwareSnapshotProvider,
    public IVssProviderCreateSnapshotSet,
    public IVssProviderNotifications
{
public:
    CSampleProvider();
    
    DECLARE_REGISTRY_RESOURCEID(IDR_SAMPLEPROVIDER)

    DECLARE_NOT_AGGREGATABLE(CSampleProvider)

    BEGIN_COM_MAP(CSampleProvider)
        COM_INTERFACE_ENTRY(IVssHardwareSnapshotProvider)
        COM_INTERFACE_ENTRY(IVssProviderCreateSnapshotSet)
        COM_INTERFACE_ENTRY(IVssProviderNotifications)
    END_COM_MAP()


    DECLARE_PROTECT_FINAL_CONSTRUCT()

    HRESULT FinalConstruct()
    {
        return S_OK;
    }

    void FinalRelease() 
    {
    }

    // IVssHardwareSnapshotProvider Methods
public:
    STDMETHOD(AreLunsSupported)( LONG lLunCount, LONG lContext, VSS_PWSZ * rgwszDevices, VDS_LUN_INFORMATION * pLunInformation, BOOL * pbIsSupported );
    STDMETHOD(FillInLunInfo)( VSS_PWSZ wszDeviceName, VDS_LUN_INFORMATION * pLunInfo, BOOL * pbIsSupported );
    STDMETHOD(BeginPrepareSnapshot)( VSS_ID SnapshotSetId, VSS_ID SnapshotId, LONG lContext, LONG lLunCount, VSS_PWSZ * rgDeviceNames, VDS_LUN_INFORMATION * rgLunInformation );
    STDMETHOD(GetTargetLuns)( LONG lLunCount, VSS_PWSZ * rgDeviceNames, VDS_LUN_INFORMATION * rgSourceLuns, VDS_LUN_INFORMATION * rgDestinationLuns );
    STDMETHOD(LocateLuns)( LONG lLunCount, VDS_LUN_INFORMATION * rgSourceLuns );
    STDMETHOD(OnLunEmpty)( VSS_PWSZ wszDeviceName, VDS_LUN_INFORMATION * pInformation );

    // IVssProviderCreateSnapshotSet Methods
public:
    STDMETHOD(EndPrepareSnapshots)(VSS_ID SnapshotSetId);
    STDMETHOD(PreCommitSnapshots)(VSS_ID SnapshotSetId);
    STDMETHOD(CommitSnapshots)(VSS_ID SnapshotSetId);
    STDMETHOD(PostCommitSnapshots)(VSS_ID SnapshotSetId, LONG lSnapshotsCount);
    STDMETHOD(PreFinalCommitSnapshots)(VSS_ID SnapshotSetId);
    STDMETHOD(PostFinalCommitSnapshots)(VSS_ID SnapshotSetId);
    STDMETHOD(AbortSnapshots)(VSS_ID SnapshotSetId);

    // IVssProviderNotifications Methods
public:
    STDMETHOD(OnLoad)(IUnknown * pCallback);
    STDMETHOD(OnUnload)(BOOL bForceUnload);

    // CSampleProvider Methods
private:
    void DeleteAbortedSnapshots();
    std::wstring SnapshotImageFile(GUID snapId);
    BOOL FindSnapId(GUID origId, GUID& snapId);
    BOOL FindOrigId(GUID snapId, GUID& origId);
    void FreeLunInfo(VDS_LUN_INFORMATION& lun);
    void CopyBasicLunInfo(VDS_LUN_INFORMATION& lunDst, VDS_LUN_INFORMATION& lunSrc);
    void DisplayLunInfo(VDS_LUN_INFORMATION& lun);

    // Member data
private:

    //
    // Vector of original LUN ids and associated snapshot
    //
    struct SnapshotInfo {
        GUID origLunId;
        GUID snapLunId;
    };
    typedef std::vector<SnapshotInfo> SnapshotInfoVector;
    SnapshotInfoVector m_vSnapshotInfo;

    //
    // Current snapshot set and state, used to detect new snapshot
    // sequences
    //
    VSS_ID m_setId;
    VSS_SNAPSHOT_STATE m_state;

    //
    // Member data lock, any access to member variables must be
    // protected with this lock
    //
    CRITICAL_SECTION m_cs;

    //
    // Virtual bus object
    //
    VirtualStorage::VirtualBus m_vbus;
};

OBJECT_ENTRY_AUTO(__uuidof(SampleProvider), CSampleProvider)
