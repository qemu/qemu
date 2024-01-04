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


// Delete all the shadow copies in the system
void VssClient::DeleteAllSnapshots()
{
    FunctionTracer ft(DBG_INFO);

    // Get list all shadow copies. 
    CComPtr<IVssEnumObject> pIEnumSnapshots;
    HRESULT hr = m_pVssObject->Query( GUID_NULL, 
            VSS_OBJECT_NONE, 
            VSS_OBJECT_SNAPSHOT, 
            &pIEnumSnapshots );

    CHECK_COM_ERROR(hr, L"m_pVssObject->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pIEnumSnapshots )")

    // If there are no shadow copies, just return
    if (hr == S_FALSE) 
    {
        ft.WriteLine(L"\nThere are no shadow copies on the system\n");
        return;
    } 

    // Enumerate all shadow copies. Delete each one
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

        // Print the deleted shadow copy...
        ft.WriteLine(L"- Deleting shadow copy " WSTR_GUID_FMT L" on %s from provider " WSTR_GUID_FMT L" [0x%08lx]...", 
            GUID_PRINTF_ARG(Snap.m_SnapshotId),
            Snap.m_pwszOriginalVolumeName,
            GUID_PRINTF_ARG(Snap.m_ProviderId),
            Snap.m_lSnapshotAttributes);

        // Perform the actual deletion
        LONG lSnapshots = 0;
        VSS_ID idNonDeletedSnapshotID = GUID_NULL;
        hr = m_pVssObject->DeleteSnapshots(
            Snap.m_SnapshotId, 
            VSS_OBJECT_SNAPSHOT,
            FALSE,
            &lSnapshots,
            &idNonDeletedSnapshotID);

        if (FAILED(hr))
        {
            ft.WriteLine(L"Error while deleting shadow copies...");
            ft.WriteLine(L"- Last shadow copy that could not be deleted: " WSTR_GUID_FMT, GUID_PRINTF_ARG(idNonDeletedSnapshotID));
            CHECK_COM_ERROR(hr, L"m_pVssObject->DeleteSnapshots(Snap.m_SnapshotId, VSS_OBJECT_SNAPSHOT,FALSE,&lSnapshots,&idNonDeleted)");
        }
    }
}


// Delete the given shadow copy set 
void VssClient::DeleteSnapshotSet(VSS_ID snapshotSetID)
{
    FunctionTracer ft(DBG_INFO);

    // Print the deleted shadow copy...
    ft.WriteLine(L"- Deleting shadow copy set " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(snapshotSetID));

    // Perform the actual deletion
    LONG lSnapshots = 0;
    VSS_ID idNonDeletedSnapshotID = GUID_NULL;
    HRESULT hr = m_pVssObject->DeleteSnapshots(
        snapshotSetID, 
        VSS_OBJECT_SNAPSHOT_SET,
        FALSE,
        &lSnapshots,
        &idNonDeletedSnapshotID);

    if (FAILED(hr))
    {
        ft.WriteLine(L"Error while deleting shadow copies...");
        ft.WriteLine(L"- Last shadow copy that could not be deleted: " WSTR_GUID_FMT, GUID_PRINTF_ARG(idNonDeletedSnapshotID));
        CHECK_COM_ERROR(hr, L"m_pVssObject->DeleteSnapshots(snapshotSetID, VSS_OBJECT_SNAPSHOT_SET,FALSE,&lSnapshots,&idNonDeleted)");
    }
}

void VssClient::DeleteOldestSnapshot(const wstring& stringVolumeName)
{
    FunctionTracer ft(DBG_INFO);

    wstring uniqueVolume = GetUniqueVolumeNameForPath(stringVolumeName);

    // Get list all shadow copies. 
    CComPtr<IVssEnumObject> pIEnumSnapshots;
    HRESULT hr = m_pVssObject->Query( GUID_NULL, 
            VSS_OBJECT_NONE, 
            VSS_OBJECT_SNAPSHOT, 
            &pIEnumSnapshots );

    CHECK_COM_ERROR(hr, L"m_pVssObject->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pIEnumSnapshots )")

    // If there are no shadow copies, just return
    if (hr == S_FALSE) 
    {
        ft.WriteLine(L"\nThere are no shadow copies on the system\n");
        return;
    } 

    // Enumerate all shadow copies. Delete each one
    VSS_OBJECT_PROP Prop;
    VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;
    
    VSS_ID OldestSnapshotId = GUID_NULL; 
    VSS_ID OldestProviderId = GUID_NULL;
    LONG OldestAttributes = 0;
    VSS_TIMESTAMP OldestSnapshotTimestamp = 0x7FFFFFFFFFFFFFFF; 
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

        if (IsEqual( Snap.m_pwszOriginalVolumeName, uniqueVolume ) &&
            OldestSnapshotTimestamp > Snap.m_tsCreationTimestamp)
        {
            OldestSnapshotId = Snap.m_SnapshotId;
            OldestSnapshotTimestamp = Snap.m_tsCreationTimestamp;
            OldestProviderId = Snap.m_ProviderId;
            OldestAttributes = Snap.m_lSnapshotAttributes;
        }
    }

    if (OldestSnapshotId == GUID_NULL)
    {
        ft.WriteLine(L"\nThere are no shadow copies on the system\n");
        return;
    }

    // Print the deleted shadow copy...
    ft.WriteLine(L"- Deleting shadow copy " WSTR_GUID_FMT L" on %s from provider " WSTR_GUID_FMT L" [0x%08lx]...", 
            GUID_PRINTF_ARG(OldestSnapshotId),
            uniqueVolume.c_str(),
            GUID_PRINTF_ARG(OldestProviderId),
            OldestAttributes);

        // Perform the actual deletion
        LONG lSnapshots = 0;
        VSS_ID idNonDeletedSnapshotID = GUID_NULL;
        hr = m_pVssObject->DeleteSnapshots(
            OldestSnapshotId, 
            VSS_OBJECT_SNAPSHOT,
            FALSE,
            &lSnapshots,
            &idNonDeletedSnapshotID);

        if (FAILED(hr))
        {
            ft.WriteLine(L"Error while deleting shadow copies...");
            ft.WriteLine(L"- Last shadow copy that could not be deleted: " WSTR_GUID_FMT, GUID_PRINTF_ARG(idNonDeletedSnapshotID));
            CHECK_COM_ERROR(hr, L"m_pVssObject->DeleteSnapshots(OldestSnapshotId, VSS_OBJECT_SNAPSHOT,FALSE,&lSnapshots,&idNonDeleted)");
        }

}

// Delete the given shadow copy
void VssClient::DeleteSnapshot(VSS_ID snapshotID)
{
    FunctionTracer ft(DBG_INFO);

    // Print the deleted shadow copy...
    ft.WriteLine(L"- Deleting shadow copy " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(snapshotID));

    // Perform the actual deletion
    LONG lSnapshots = 0;
    VSS_ID idNonDeletedSnapshotID = GUID_NULL;
    HRESULT hr = m_pVssObject->DeleteSnapshots(
        snapshotID, 
        VSS_OBJECT_SNAPSHOT,
        FALSE,
        &lSnapshots,
        &idNonDeletedSnapshotID);

    if (FAILED(hr))
    {
        ft.WriteLine(L"Error while deleting shadow copies...");
        ft.WriteLine(L"- Last shadow copy that could not be deleted: " WSTR_GUID_FMT, GUID_PRINTF_ARG(idNonDeletedSnapshotID));
        CHECK_COM_ERROR(hr, L"m_pVssObject->DeleteSnapshots(snapshotID, VSS_OBJECT_SNAPSHOT,FALSE,&lSnapshots,&idNonDeleted)");
    }
}

