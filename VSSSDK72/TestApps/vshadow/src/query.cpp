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


// Query all the shadow copies in the given set
// If snapshotSetID is NULL, just query all shadow copies in the system
void VssClient::QuerySnapshotSet(VSS_ID snapshotSetID)
{
    FunctionTracer ft(DBG_INFO);

    if (snapshotSetID == GUID_NULL)
        ft.WriteLine(L"\nQuerying all shadow copies in the system ...\n");
    else
        ft.WriteLine(L"\nQuerying all shadow copies with the SnapshotSetID " WSTR_GUID_FMT L" ...\n", GUID_PRINTF_ARG(snapshotSetID));
    
    // Get list all shadow copies. 
    CComPtr<IVssEnumObject> pIEnumSnapshots;
    HRESULT hr = m_pVssObject->Query( GUID_NULL, 
            VSS_OBJECT_NONE, 
            VSS_OBJECT_SNAPSHOT, 
            &pIEnumSnapshots );

    CHECK_COM_ERROR(hr, L"m_pVssObject->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pIEnumSnapshots )")

    // If there are no shadow copies, just return
    if (hr == S_FALSE) {
        if (snapshotSetID == GUID_NULL)
            ft.WriteLine(L"\nThere are no shadow copies in the system\n");
        return;
    } 

    // Enumerate all shadow copies. 
    VSS_OBJECT_PROP Prop;
    VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;
    
    while(true)
    {
        // Get the next element
        ULONG ulFetched;
        hr = pIEnumSnapshots->Next( 1, &Prop, &ulFetched );
        CHECK_COM_ERROR(hr, L"pIEnumSnapshots->Next( 1, &Prop, &ulFetched )")

        // We reached the end of list
        if (ulFetched == 0)
            break;

        // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
        CAutoSnapPointer snapAutoCleanup(&Snap);

        // Print the shadow copy (if not filtered out)
        if ((snapshotSetID == GUID_NULL) || (Snap.m_SnapshotSetId == snapshotSetID))
            PrintSnapshotProperties(Snap);
    }
}


// Query the properties of the given shadow copy
void VssClient::GetSnapshotProperties(VSS_ID snapshotID)
{
    FunctionTracer ft(DBG_INFO);

    // Get the shadow copy properties
    VSS_SNAPSHOT_PROP Snap;
    CHECK_COM(m_pVssObject->GetSnapshotProperties(snapshotID, &Snap));

    // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
    CAutoSnapPointer snapAutoCleanup(&Snap);

    // Print the properties of this shadow copy 
    PrintSnapshotProperties(Snap);
}


// Print the properties for the given snasphot
void VssClient::PrintSnapshotProperties(VSS_SNAPSHOT_PROP & prop)
{
    FunctionTracer ft(DBG_INFO);
    
    LONG lAttributes = prop.m_lSnapshotAttributes;

    ft.WriteLine(L"* SNAPSHOT ID = " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(prop.m_SnapshotId));
    ft.WriteLine(L"   - Shadow copy Set: " WSTR_GUID_FMT, GUID_PRINTF_ARG(prop.m_SnapshotSetId));
    ft.WriteLine(L"   - Original count of shadow copies = %d", prop.m_lSnapshotsCount);
    ft.WriteLine(L"   - Original Volume name: %s [%s]", 
        prop.m_pwszOriginalVolumeName, 
        GetDisplayNameForVolume(prop.m_pwszOriginalVolumeName).c_str()
        );
    ft.WriteLine(L"   - Creation Time: %s", VssTimeToString(prop.m_tsCreationTimestamp).c_str());
    ft.WriteLine(L"   - Shadow copy device name: %s", prop.m_pwszSnapshotDeviceObject);
    ft.WriteLine(L"   - Originating machine: %s", prop.m_pwszOriginatingMachine);
    ft.WriteLine(L"   - Service machine: %s", prop.m_pwszServiceMachine);

    if (prop.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY)
        ft.WriteLine(L"   - Exposed locally as: %s", prop.m_pwszExposedName);
    else if (prop.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY) 
    {
        ft.WriteLine(L"   - Exposed remotely as %s", prop.m_pwszExposedName);
        if (prop.m_pwszExposedPath && wcslen(prop.m_pwszExposedPath) > 0)
            ft.WriteLine(L"   - Path exposed: %s", prop.m_pwszExposedPath);
    }
    else
        ft.WriteLine(L"   - Not Exposed");

    ft.WriteLine(L"   - Provider id: " WSTR_GUID_FMT, GUID_PRINTF_ARG(prop.m_ProviderId));

    // Display the attributes
    wstring attributes;
    if (lAttributes & VSS_VOLSNAP_ATTR_TRANSPORTABLE)
        attributes  += wstring(L" Transportable");
    
    if (lAttributes & VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE)
        attributes  += wstring(L" No_Auto_Release");
    else
        attributes  += wstring(L" Auto_Release");

    if (lAttributes & VSS_VOLSNAP_ATTR_PERSISTENT)
        attributes  += wstring(L" Persistent");

    if (lAttributes & VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE)
        attributes  += wstring(L" Client_accessible");

    if (lAttributes & VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED)
        attributes  += wstring(L" Hardware");

    if (lAttributes & VSS_VOLSNAP_ATTR_NO_WRITERS)
        attributes  += wstring(L" No_Writers");

    if (lAttributes & VSS_VOLSNAP_ATTR_IMPORTED)
        attributes  += wstring(L" Imported");

    if (lAttributes & VSS_VOLSNAP_ATTR_PLEX)
        attributes  += wstring(L" Plex");
    
    if (lAttributes & VSS_VOLSNAP_ATTR_DIFFERENTIAL)
        attributes  += wstring(L" Differential");

    ft.WriteLine(L"   - Attributes: %s", attributes.c_str());
    
    ft.WriteLine(L"");
}

