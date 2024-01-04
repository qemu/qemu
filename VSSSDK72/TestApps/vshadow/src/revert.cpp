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

#ifdef VSS_SERVER

HRESULT APIENTRY ShouldBlockRevert(IN LPCWSTR wszVolumeName, OUT bool* pbBlock);

void VssClient::RevertToSnapshot(VSS_ID snapshotID)
{
    FunctionTracer ft(DBG_INFO);

    // Get the shadow copy properties
    VSS_SNAPSHOT_PROP Snap;
    CHECK_COM(m_pVssObject->GetSnapshotProperties(snapshotID, &Snap));
    
    // Automatically call VssFreeSnapshotProperties on this structure at the end of scope
    CAutoSnapPointer snapAutoCleanup(&Snap);

    ft.WriteLine(L"- Reverting to shadow copy " WSTR_GUID_FMT L" on %s from provider " WSTR_GUID_FMT L" [0x%08lx]...", 
            GUID_PRINTF_ARG(Snap.m_SnapshotId),
            Snap.m_pwszOriginalVolumeName,
            GUID_PRINTF_ARG(Snap.m_ProviderId),
            Snap.m_lSnapshotAttributes);
    
    bool bBlock = false;
    CHECK_COM(::ShouldBlockRevert(Snap.m_pwszOriginalVolumeName, &bBlock));
    if (bBlock)
    {
        ft.WriteLine(L"Revert is disabled on the volume %s because of writers",
                Snap.m_pwszOriginalVolumeName);
        return;
    }

    HRESULT hr = m_pVssObject->RevertToSnapshot(snapshotID, true);
    if (FAILED(hr))
    {
        switch (hr)
        {
        case VSS_E_OBJECT_NOT_FOUND:
                ft.WriteLine(L"Shadow Copy with id " WSTR_GUID_FMT L" was not found",
                    GUID_PRINTF_ARG(snapshotID));
                return;
            case VSS_E_VOLUME_IN_USE:
                ft.WriteLine(L"The voulume %s cannot be reverted since it is in use",
                    Snap.m_pwszOriginalVolumeName);
                return;
            case VSS_E_REVERT_IN_PROGRESS:
                ft.WriteLine(L"A revert is current in progress on the volume %s",
                    Snap.m_pwszOriginalVolumeName);
                return;
            case VSS_E_VOLUME_NOT_SUPPORTED:
                ft.WriteLine(L"Revert is not supported on the volume %s",
                    Snap.m_pwszOriginalVolumeName);
                return;            
            default:
                ft.WriteLine(L"RevertToSnapshot on Shadow Copy " WSTR_GUID_FMT L" failed with error 0x%08lx",
                            GUID_PRINTF_ARG(snapshotID),
                            hr);
                return;
        }
    }

    CComPtr<IVssAsync> pAsync;
    hr = m_pVssObject->QueryRevertStatus(Snap.m_pwszOriginalVolumeName, &pAsync);
    if (hr != VSS_E_OBJECT_NOT_FOUND)
    {
        if (FAILED(hr))
        {
            ft.WriteLine(L"QueryRevertStatus failed with error 0x%08lx", hr);
            ft.WriteLine(L"Revert may still be in progress, but cannot be tracked");
            return;
        }

        hr = pAsync->Wait();
        if (FAILED(hr))
        {
            ft.WriteLine(L"IVssAsync::Wait failed with error 0x%08lx", hr);
            ft.WriteLine(L"Revert may still be in progress, but cannot be tracked");
            return;
        }
    }

    ft.WriteLine(L"The shadow copy has been successfully reverted");
}

#endif