/////////////////////////////////////////////////////////////////////////
// Copyright © 2004 Microsoft Corporation. All rights reserved.
// 
//  This file may contain preliminary information or inaccuracies, 
//  and may not correctly represent any associated Microsoft 
//  Product as commercially released. All Materials are provided entirely 
//  “AS IS.” To the extent permitted by law, MICROSOFT MAKES NO 
//  WARRANTY OF ANY KIND, DISCLAIMS ALL EXPRESS, IMPLIED AND STATUTORY 
//  WARRANTIES, AND ASSUMES NO LIABILITY TO YOU FOR ANY DAMAGES OF 
//  ANY TYPE IN CONNECTION WITH THESE MATERIALS OR ANY INTELLECTUAL PROPERTY IN THEM. 
// 


// Main header
#include "stdafx.h"


// CLSID for the VDS loader
const GUID      CLSID_VdsLoader = {0x9C38ED61,0xD565,0x4728,{0xAE,0xEE,0xC8,0x09,0x52,0xF0,0xEC,0xDE}};

//
// Break the given shadow copy set to read or read-write
//
// Setting the parameter pVolumeNames makes this function return immediately after VSS breaks the
// shadow copy set. This is useful in the fast recover scenario: if the LUN after break is in an
// offline mode, the requster must wait for the LUN online in order to call VDS to make it read-write.
void VssClient::BreakSnapshotSet(VSS_ID snapshotSetID, bool makeReadWrite, vector<wstring> *pVolumeNames)
{
    FunctionTracer ft(DBG_INFO);

    if (makeReadWrite)
    {
        // If we want read-write treatment, compute a list of volumes in the shadow copy set
        vector<wstring> snapshotDeviceList;
        snapshotDeviceList = GetSnapshotDevices(snapshotSetID);

        ft.WriteLine(L"- Calling BreakSnapshotSet on " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(snapshotSetID));

        // Break the shadow copy set
        CHECK_COM(m_pVssObject->BreakSnapshotSet(snapshotSetID));

        // If we want to delay read-write treatment, fill out the volume name list and return
        if (pVolumeNames)
        {
            *pVolumeNames = snapshotDeviceList;
            return;
        }

        ft.WriteLine(L"- Making shadow copy devices from " WSTR_GUID_FMT L" read-write...", GUID_PRINTF_ARG(snapshotSetID));

        // Make the snapshot devices read-write
        MakeVolumesReadWrite(snapshotDeviceList);
    }
    else
    {
        ft.WriteLine(L"- Calling BreakSnapshotSet on " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(snapshotSetID));

        // Just break the snapshot set
        CHECK_COM(m_pVssObject->BreakSnapshotSet(snapshotSetID));
    }
}


// Return the list of snapshot volume devices in this snapshot set
vector<wstring> VssClient::GetSnapshotDevices(VSS_ID snapshotSetID)
{
    FunctionTracer ft(DBG_INFO);

    vector<wstring> volumes;
        
    // Get list all snapshots. 
    CComPtr<IVssEnumObject> pIEnumSnapshots;
    CHECK_COM( m_pVssObject->Query( GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pIEnumSnapshots ) );

    // Enumerate all snapshots. 
    VSS_OBJECT_PROP Prop;
    VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;
    while(true)
    {
        // Get the next element
        ULONG ulFetched = 0;
        CHECK_COM(pIEnumSnapshots->Next( 1, &Prop, &ulFetched ));

        // We reached the end of list
        if (ulFetched == 0)
            break;

        // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
        CAutoSnapPointer snapAutoCleanup(&Snap);

        // Ignore snapshots not part of this set
        if (Snap.m_SnapshotSetId == snapshotSetID)
        {
            // Get the snapshot device object name which is a volume guid name for persistent snapshot
            // and a device name for non persistent snapshot.
            // The volume guid name and the device name we obtained here might change after breaksnapshot
            // depending on if the disk signature is reverted, but those cached names should still work
            // as symbolic links, in which case they can not persist after reboot.
            wstring snapshotDeviceObjectName = Snap.m_pwszSnapshotDeviceObject;

            // Add it to the array
            ft.WriteLine(L"- Will convert %s to read-write ...", snapshotDeviceObjectName.c_str());
            volumes.push_back(snapshotDeviceObjectName);
        }
    }

    // Return the list of snapshot volumes
    return volumes;
}



////////////////////////////////////////////////////////////////////////////
// VDS API calls 
//

// Make the volumes in this list read-write using VDS API
void VssClient::MakeVolumesReadWrite(vector<wstring> snapshotVolumes)
{
    FunctionTracer ft(DBG_INFO);

    ft.Trace(DBG_INFO, L"Clearing read-only on %d volumes ... ", snapshotVolumes.size());

    // Get the VDS loader
    CComPtr<IVdsServiceLoader> pLoader;
    CHECK_COM(CoCreateInstance(CLSID_VdsLoader,
        NULL,
        CLSCTX_LOCAL_SERVER,
        __uuidof(IVdsServiceLoader),
        (void **)&pLoader));

    // Get the service interface pointer
    CComPtr<IVdsService> pService;
    CHECK_COM(pLoader->LoadService(NULL, &pService));

    vector<wstring> clearedVolumes;

    // Get the unique volume names for the cached snapshot volume names 
    // which might change after the break
    vector<wstring> snapshotVolumeUniqueNames;
    for (unsigned i = 0; i < snapshotVolumes.size( ); i++)
        snapshotVolumeUniqueNames.push_back(GetUniqueVolumeNameForMountPoint(snapshotVolumes[i]));


    // Enumerate the Software providers
    CComPtr<IEnumVdsObject> pEnumProvider;
    CHECK_COM(pService->QueryProviders(VDS_QUERY_SOFTWARE_PROVIDERS,&pEnumProvider));
    vector< CComPtr<IUnknown> > providers = EnumerateVdsObjects(pEnumProvider);
    for(unsigned iProvider = 0; iProvider < providers.size(); iProvider++)
    {
        // QueryInterface for IVdsSwProvider
        CComQIPtr<IVdsSwProvider> pSwProvider = providers[iProvider];

        ft.Trace(DBG_INFO, L"- Provider %d", iProvider);

        // Enumerate packs for this provider
        CComPtr<IEnumVdsObject> pEnumPack;
        CHECK_COM(pSwProvider->QueryPacks(&pEnumPack));
        vector< CComPtr<IUnknown> > packs = EnumerateVdsObjects(pEnumPack);
        for(unsigned iPack = 0; iPack < packs.size(); iPack++)
        {
            // QueryInterface for IVdsPack
            CComQIPtr<IVdsPack> pPack = packs[iPack];

            ft.Trace(DBG_INFO, L"- Pack %d/%d", iPack, iProvider);

            // Enumerate volumes
            CComPtr<IEnumVdsObject> pEnumVolumes;
            CHECK_COM(pPack->QueryVolumes(&pEnumVolumes));
            vector< CComPtr<IUnknown> > volumes = EnumerateVdsObjects(pEnumVolumes);
            for(unsigned iVol = 0; iVol < volumes.size(); iVol++)
            {
                // QueryInterface for IVdsVolumeMF and IVdsVolume
                CComQIPtr<IVdsVolume> pVolume = volumes[iVol];

                // Get volume properties. Ignore deleted volumes
                VDS_VOLUME_PROP volProp;
                HRESULT hr = pVolume->GetProperties(&volProp);
                if (hr == VDS_E_OBJECT_DELETED )
                    continue;

                CHECK_COM_ERROR(hr, L"pVolume->GetProperties(&volProp)");

                // Skip a hidden volume (it fails GetVolumeNameForMountPoint)
                if (volProp.ulFlags & VDS_VF_HIDDEN)
                    continue;

                // Automatically call CoTaskMemFree on this pointer at the end of scope
                CAutoComPointer ptrAutoCleanup(volProp.pwszName);

                // Get the initial device name (normally with the format \\?\GLOBALROOT\Device\HarddiskVolumeXX)
                wstring name = volProp.pwszName;

                // Get the unique volume guid name for this device name.
                wstring uniqueVolumeName = GetUniqueVolumeNameForMountPoint(name);

                ft.Trace(DBG_INFO, L"- Found volume %s [device = %s] in %d/%d", 
                    uniqueVolumeName.c_str(), name.c_str(), iPack, iProvider);

                // Check to see if this is one of our volumes. If not, continue
                if (!FindStringInList(uniqueVolumeName, snapshotVolumeUniqueNames))
                    continue;

                // Clear the read-only flag
                ft.WriteLine(L"- Clearing read-only flag for volume %s [%s] ...", uniqueVolumeName.c_str(), name.c_str());

                CHECK_COM(pVolume->ClearFlags(VDS_VF_READONLY));

                // Force-dismounts the volume 
                // since we want to re-mount the file system as read-write
                CComQIPtr<IVdsVolumeMF> pVolumeMF = pVolume;
                ft.WriteLine(L"- Dismounting volume %s ...", name.c_str());
                CHECK_COM(pVolumeMF->Dismount(TRUE, FALSE));

                clearedVolumes.push_back(uniqueVolumeName);
            }
        }
    }

    // Check that all volumes have been cleared ...
    if (clearedVolumes.size() != snapshotVolumeUniqueNames.size())
    {
        ft.WriteLine(L"WARNING: some volumes were not succesfully converted to read-write!");

        for (unsigned i = 0; i < snapshotVolumeUniqueNames.size(); i++)
            if (!FindStringInList(snapshotVolumeUniqueNames[i], clearedVolumes))
                ft.WriteLine(L"- Volume %s not found on the system. Clearing the read-only flag failed on it.",
                    snapshotVolumeUniqueNames[i].c_str());
    }
}


// Returns an array of enumerated VDS objects
vector< CComPtr<IUnknown> > VssClient::EnumerateVdsObjects(IEnumVdsObject * pEnumeration)
{
    FunctionTracer ft(DBG_INFO);

    vector< CComPtr<IUnknown> > objectList;
    
    while(true)
    {
        CComPtr<IUnknown> pUnknown;
        ULONG ulFetched = 0;
        CHECK_COM(pEnumeration->Next(1, &pUnknown, &ulFetched));

        // End of enumeration
        if (ulFetched == 0)
            break;

        // Add the object to the array
        objectList.push_back(pUnknown);
    }

    return objectList;
}
